module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>
#include <memory>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <thread>
#include <optional>
#include <utility>
#include <vector>
#endif
#if defined(_WIN32)
#include <DescriptorHeap.h>
#endif // defined(_WIN32)
module fyuu_rhi:d3d12_descriptor_allocator;
#if defined(_WIN32)
#if defined(__cpp_lib_modules)
import std;
#endif

namespace {

	class DescriptorHeap : public DirectX::DescriptorHeap {
	private:
		std::vector<bool> m_allocated;
		std::mutex m_mutex;
		std::condition_variable m_condition;

	public:
		DescriptorHeap(
			Microsoft::WRL::ComPtr<ID3D12Device> const& device,
			D3D12_DESCRIPTOR_HEAP_TYPE type,
			std::size_t total_desc,
			bool shader_visible
		)
			: DirectX::DescriptorHeap(
				device.Get(),
				type,
				shader_visible ?
					D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE :
					D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
				total_desc
			),
			m_allocated(total_desc, false) {
		}

		std::size_t Acquire(std::size_t count) {
			if (count == 0u || count > m_allocated.size()) {
				throw std::invalid_argument("DescriptorHeap::Acquire(): invalid descriptor count");
			}
			std::unique_lock<std::mutex> lock(m_mutex);
			for (;;) {
				std::size_t available = 0u;
				for (std::size_t index = 0u; index < m_allocated.size(); ++index) {
					available = m_allocated[index] ? 0u : available + 1u;
					if (available != count) {
						continue;
					}
					auto first = index + 1u - count;
					for (std::size_t current = first; current <= index; ++current) {
						m_allocated[current] = true;
					}
					return first;
				}
				m_condition.wait(lock);
			}
		}

		void Release(std::size_t index, std::size_t count) {
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				for (std::size_t current = index; current < index + count; ++current) {
					m_allocated[current] = false;
				}
			}
			m_condition.notify_one();
		}
	};

}

namespace fyuu_rhi::d3d12 {
	class ManagedDescriptorHeap final {
	private:
		std::shared_ptr<DescriptorHeap> m_heap;

	public:
		ManagedDescriptorHeap() noexcept = default;
		explicit ManagedDescriptorHeap(std::shared_ptr<DescriptorHeap> const& heap) noexcept
			: m_heap(heap) {
		}

		[[nodiscard]] ID3D12DescriptorHeap* Native() const noexcept {
			return m_heap ? m_heap->Heap() : nullptr;
		}
	};

	class ManagedDescriptorHandle final {
	private:
		std::shared_ptr<DescriptorHeap> m_heap;
		std::optional<std::size_t> m_idx;
		D3D12_CPU_DESCRIPTOR_HANDLE m_cpu;
		D3D12_GPU_DESCRIPTOR_HANDLE m_gpu;

	public:
		ManagedDescriptorHandle(std::shared_ptr<DescriptorHeap> heap, std::size_t idx, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu) noexcept
			: m_heap(std::move(heap)), m_idx(idx), m_cpu(cpu), m_gpu(gpu) {
		}

		ManagedDescriptorHandle(std::shared_ptr<DescriptorHeap> heap, std::size_t idx, D3D12_CPU_DESCRIPTOR_HANDLE cpu) noexcept
			: m_heap(std::move(heap)), m_idx(idx), m_cpu(cpu), m_gpu() {
		}

		ManagedDescriptorHandle(ManagedDescriptorHandle const&) = delete;
		ManagedDescriptorHandle& operator=(ManagedDescriptorHandle const&) = delete;

		ManagedDescriptorHandle(ManagedDescriptorHandle&& other) noexcept
			: m_heap(std::move(other.m_heap)),
			m_idx(std::move(other.m_idx)),
			m_cpu(std::exchange(other.m_cpu, {})),
			m_gpu(std::exchange(other.m_gpu, {})) {
			other.m_idx.reset();
		}

		ManagedDescriptorHandle& operator=(ManagedDescriptorHandle&& other) noexcept {
			if (this != &other) {
				if (m_heap && m_idx) {
					m_heap->Release(*m_idx, 1u);
				}
				m_heap = std::move(other.m_heap);
				m_idx = std::move(other.m_idx);
				other.m_idx.reset();
				m_cpu = std::exchange(other.m_cpu, {});
				m_gpu = std::exchange(other.m_gpu, {});
			}
			return *this;
		}

		~ManagedDescriptorHandle() {
			if (m_heap && m_idx) {
				m_heap->Release(*m_idx, 1u);
			}
		}

		D3D12_CPU_DESCRIPTOR_HANDLE CPU() const noexcept {
			return m_cpu;
		}

		D3D12_GPU_DESCRIPTOR_HANDLE GPU() const noexcept {
			return m_gpu;
		}
	};

	class ManagedDescriptorRange final {
	private:
		std::shared_ptr<DescriptorHeap> m_heap;
		std::optional<std::size_t> m_index;
		std::size_t m_count = 0u;

	public:
		ManagedDescriptorRange(
			std::shared_ptr<DescriptorHeap> const& heap,
			std::size_t index,
			std::size_t count
		) noexcept : m_heap(heap), m_index(index), m_count(count) {
		}

		ManagedDescriptorRange(ManagedDescriptorRange const&) = delete;
		ManagedDescriptorRange& operator=(ManagedDescriptorRange const&) = delete;
		ManagedDescriptorRange(ManagedDescriptorRange&& other) noexcept
			: m_heap(other.m_heap),
			m_index(std::exchange(other.m_index, std::nullopt)),
			m_count(std::exchange(other.m_count, 0u)) {
		}
		ManagedDescriptorRange& operator=(ManagedDescriptorRange&& other) noexcept {
			std::swap(m_heap, other.m_heap);
			std::swap(m_index, other.m_index);
			std::swap(m_count, other.m_count);
			return *this;
		}
		~ManagedDescriptorRange() noexcept {
			if (m_heap && m_index) {
				m_heap->Release(*m_index, m_count);
			}
		}

		[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE CPU(std::size_t offset = 0u) const {
			if (offset >= m_count) throw std::out_of_range("ManagedDescriptorRange::CPU(): invalid offset");
			return m_heap->GetCpuHandle(*m_index + offset);
		}

		[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GPU(std::size_t offset = 0u) const {
			if (offset >= m_count) throw std::out_of_range("ManagedDescriptorRange::GPU(): invalid offset");
			return m_heap->GetGpuHandle(*m_index + offset);
		}

		[[nodiscard]] ID3D12DescriptorHeap* Heap() const noexcept {
			return m_heap->Heap();
		}
	};

	class DescriptorAllocator final {
	private:
		std::shared_ptr<DescriptorHeap> m_heap;

	public:
		DescriptorAllocator(
			Microsoft::WRL::ComPtr<ID3D12Device> const& device,
			D3D12_DESCRIPTOR_HEAP_TYPE type,
			std::size_t total_desc,
			bool shader_visible
		) : m_heap(std::make_shared<DescriptorHeap>(device, type, total_desc, shader_visible)) {
		}

		DescriptorAllocator(DescriptorAllocator const& other) noexcept = default;
		DescriptorAllocator& operator=(DescriptorAllocator const& other) noexcept = default;
		DescriptorAllocator(DescriptorAllocator&& other) noexcept = default;
		DescriptorAllocator& operator=(DescriptorAllocator&& other) noexcept = default;

		ManagedDescriptorHandle Allocate() const {
			std::size_t idx = m_heap->Acquire(1u);
			D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = m_heap->GetCpuHandle(idx);
			if (m_heap->Flags() & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) {
				D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle = m_heap->GetGpuHandle(idx);
				return ManagedDescriptorHandle(m_heap, idx, cpu_handle, gpu_handle);
			}
			else {
				return ManagedDescriptorHandle(m_heap, idx, cpu_handle);
			}
		}

		ManagedDescriptorRange Allocate(std::size_t count) const {
			return ManagedDescriptorRange(m_heap, m_heap->Acquire(count), count);
		}


		ID3D12DescriptorHeap* GetNative() const {
			return m_heap->Heap();
		}

		ManagedDescriptorHeap GetHeap() const noexcept {
			return ManagedDescriptorHeap(m_heap);
		}

	};

	inline DescriptorAllocator CreateUniversalViewAllocator(
		Microsoft::WRL::ComPtr<ID3D12Device> const& device,
		std::size_t total_desc = 65536u
	) {
		return DescriptorAllocator(
			device,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			total_desc,
			true
		);
	}

	inline DescriptorAllocator CreateRTVAllocator(
		Microsoft::WRL::ComPtr<ID3D12Device> const& device,
		std::size_t total_desc = 2048u
	)  {
		return DescriptorAllocator(
			device,
			D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
			total_desc,
			false
		);
	}

	inline DescriptorAllocator CreateDSVAllocator(
		Microsoft::WRL::ComPtr<ID3D12Device> const& device,
		std::size_t total_desc = 2048u
	) {
		return DescriptorAllocator(
			device,
			D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
			total_desc,
			false
		);
	}
	
	inline DescriptorAllocator CreateSamplerAllocator(
		Microsoft::WRL::ComPtr<ID3D12Device> const& device,
		std::size_t total_desc = 2048u
	) {
		return DescriptorAllocator(
			device,
			D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
			total_desc,
			true
		);
	}

} // namespace fyuu_rhi::d3d12

#endif // _WIN32
