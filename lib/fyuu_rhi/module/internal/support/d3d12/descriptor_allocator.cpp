module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
#include <algorithm>

#include <condition_variable>
#include <mutex>

#include <optional>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <DescriptorHeap.h>
#include <d3d12.h>
#include <wrl.h>
#endif // defined(_WIN32)

module fyuu_rhi:d3d12_descriptor_allocator;
#if defined(_WIN32)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

namespace {

	class DescriptorHeap final : public DirectX::DescriptorHeap {
	private:
		std::vector<bool> m_allocated;
		std::mutex m_mutex;
		std::condition_variable m_condition;

	public:
		DescriptorHeap(
			Microsoft::WRL::ComPtr<ID3D12Device> const& device,
			D3D12_DESCRIPTOR_HEAP_TYPE type,
			std::size_t descriptor_count,
			bool shader_visible
		) : DirectX::DescriptorHeap(
				device.Get(),
				type,
				shader_visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
				descriptor_count
			),
			m_allocated(descriptor_count, false) {
		}

		std::size_t Acquire(std::size_t count) {
			if (count == 0u || count > m_allocated.size()) {
				throw std::invalid_argument(
					"The D3D12 descriptor count is outside the heap capacity"
				);
			}
			std::unique_lock lock(m_mutex);
			std::vector<bool>::iterator available;
			m_condition.wait(
				lock,
				[&]
				{
					available = std::search_n(
						m_allocated.begin(),
						m_allocated.end(),
						count,
						false
					);
					return available != m_allocated.end();
				}
			);
			auto index = static_cast<std::size_t>(
				std::distance(m_allocated.begin(), available)
			);
			std::fill_n(available, count, true);
			return index;
		}

		void Release(std::size_t index, std::size_t count) noexcept {
			{
				std::unique_lock lock(m_mutex);
				std::fill_n(m_allocated.begin() + index, count, false);
			}
			m_condition.notify_one();
		}
	};

} // namespace

namespace fyuu_rhi::d3d12 {

	class ManagedDescriptorHandle final {
	private:
		std::shared_ptr<DescriptorHeap> m_heap;
		std::optional<std::size_t> m_index;
		D3D12_CPU_DESCRIPTOR_HANDLE m_cpu;
		D3D12_GPU_DESCRIPTOR_HANDLE m_gpu;

		void Release() noexcept {
			if (m_heap && m_index) {
				m_heap->Release(*m_index, 1u);
				m_index.reset();
			}
		}

	public:
		ManagedDescriptorHandle() noexcept = default;
		ManagedDescriptorHandle(
			std::shared_ptr<DescriptorHeap> const& heap,
			std::size_t index,
			D3D12_CPU_DESCRIPTOR_HANDLE cpu,
			D3D12_GPU_DESCRIPTOR_HANDLE gpu
		) noexcept
			: m_heap(heap),
			m_index(index),
			m_cpu(cpu),
			m_gpu(gpu) {
		}

		ManagedDescriptorHandle(ManagedDescriptorHandle const&) = delete;
		ManagedDescriptorHandle& operator=(ManagedDescriptorHandle const&) = delete;

		ManagedDescriptorHandle(ManagedDescriptorHandle&& other) noexcept
			: m_heap(std::move(other.m_heap)),
			m_index(std::exchange(other.m_index, std::nullopt)),
			m_cpu(std::exchange(other.m_cpu, {})),
			m_gpu(std::exchange(other.m_gpu, {})) {
		}

		ManagedDescriptorHandle& operator=(ManagedDescriptorHandle&& other) noexcept {
			if (this != &other) {
				Release();
				m_heap = std::move(other.m_heap);
				m_index = std::exchange(other.m_index, std::nullopt);
				m_cpu = std::exchange(other.m_cpu, {});
				m_gpu = std::exchange(other.m_gpu, {});
			}
			return *this;
		}

		~ManagedDescriptorHandle() noexcept {
			Release();
		}

		D3D12_CPU_DESCRIPTOR_HANDLE CPU() const noexcept {
			return m_cpu;
		}

		D3D12_GPU_DESCRIPTOR_HANDLE GPU() const noexcept {
			return m_gpu;
		}

		void ValidateDevice(
			Microsoft::WRL::ComPtr<ID3D12Device> const& device
		) const {
			if (!m_heap) {
				throw std::invalid_argument("The D3D12 descriptor is empty");
			}
			Microsoft::WRL::ComPtr<ID3D12Device> descriptor_device;
			auto result = m_heap->Heap()->GetDevice(
				IID_PPV_ARGS(&descriptor_device)
			);
			if (FAILED(result)) {
				throw std::runtime_error("Failed to query the D3D12 descriptor device");
			}
			if (descriptor_device != device) {
				throw std::invalid_argument(
					"The D3D12 descriptor belongs to another logical device"
				);
			}
		}
	};

	class ManagedDescriptorHeap final {
	private:
		std::shared_ptr<DescriptorHeap> m_heap;

	public:
		explicit ManagedDescriptorHeap(std::shared_ptr<DescriptorHeap> const& heap) noexcept
			: m_heap(heap) {
		}

		ID3D12DescriptorHeap* Native() const noexcept {
			return m_heap ? m_heap->Heap() : nullptr;
		}
	};

	class ManagedDescriptorRange final {
	private:
		std::shared_ptr<DescriptorHeap> m_heap;
		std::optional<std::size_t> m_index;
		std::size_t m_count;

	public:
		ManagedDescriptorRange(
			std::shared_ptr<DescriptorHeap> const& heap,
			std::size_t index,
			std::size_t count
		) noexcept
			: m_heap(heap),
			m_index(index),
			m_count(count) {
		}

		ManagedDescriptorRange(ManagedDescriptorRange const&) = delete;
		ManagedDescriptorRange& operator=(ManagedDescriptorRange const&) = delete;

		ManagedDescriptorRange(ManagedDescriptorRange&& other) noexcept
			: m_heap(std::move(other.m_heap)),
			m_index(std::exchange(other.m_index, std::nullopt)),
			m_count(std::exchange(other.m_count, 0u)) {
		}

		ManagedDescriptorRange& operator=(
			ManagedDescriptorRange&& other
		) noexcept {
			if (this != &other) {
				if (m_heap && m_index) {
					m_heap->Release(*m_index, m_count);
				}
				m_heap = std::move(other.m_heap);
				m_index = std::exchange(other.m_index, std::nullopt);
				m_count = std::exchange(other.m_count, 0u);
			}
			return *this;
		}

		~ManagedDescriptorRange() noexcept {
			if (m_heap && m_index) {
				m_heap->Release(*m_index, m_count);
			}
		}

		D3D12_CPU_DESCRIPTOR_HANDLE CPU(
			std::size_t offset = 0u
		) const {
			if (offset >= m_count) {
				throw std::out_of_range(
					"The D3D12 CPU descriptor offset is outside the range"
				);
			}
			return m_heap->GetCpuHandle(*m_index + offset);
		}

		D3D12_GPU_DESCRIPTOR_HANDLE GPU(
			std::size_t offset = 0u
		) const {
			if (offset >= m_count) {
				throw std::out_of_range(
					"The D3D12 GPU descriptor offset is outside the range"
				);
			}
			return m_heap->GetGpuHandle(*m_index + offset);
		}
	};

	class DescriptorAllocator final {
	private:
		std::shared_ptr<DescriptorHeap> m_heap;

	public:
		DescriptorAllocator(
			Microsoft::WRL::ComPtr<ID3D12Device> const& device,
			D3D12_DESCRIPTOR_HEAP_TYPE type,
			std::size_t descriptor_count,
			bool shader_visible
		) : m_heap(
				std::make_shared<DescriptorHeap>(
					device,
					type,
					descriptor_count,
					shader_visible
				)
			) {
		}

		ManagedDescriptorHandle Allocate() const {
			auto index = m_heap->Acquire(1u);
			return ManagedDescriptorHandle(
				m_heap,
				index,
				m_heap->GetCpuHandle(index),
				m_heap->Flags() & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE ? 
					m_heap->GetGpuHandle(index) : 
					D3D12_GPU_DESCRIPTOR_HANDLE{}
			);
		}

		ManagedDescriptorRange Allocate(std::size_t count) const {
			return ManagedDescriptorRange(
				m_heap,
				m_heap->Acquire(count),
				count
			);
		}

		ManagedDescriptorHeap Heap() const noexcept {
			return ManagedDescriptorHeap(m_heap);
		}

		ID3D12DescriptorHeap* Native() const noexcept {
			return m_heap->Heap();
		}
	};

} // namespace fyuu_rhi::d3d12
#endif // defined(_WIN32)
