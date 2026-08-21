module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <exception>
#include <format>
#include <stdexcept>
#include <string>
#endif // !defined(__cpp_lib_modules)
#if defined(__APPLE__)
#include <Metal/Metal.hpp>
#endif // defined(__APPLE__)

module fyuu_rhi:metal_completion_token;
#if defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :completion_token_dispatch;
import :metal_data;

namespace fyuu_rhi::execution {
	namespace {
		std::string ErrorMessage(NS::Error* error) {
			if (!error || !error->localizedDescription()) {
				return "No Metal diagnostic message";
			}
			return error->localizedDescription()->utf8String();
		}
	}

	template <>
	struct PollCompletionToken<metal::CompletionToken> {
		metal::CompletionToken* token;

		bool operator()() const noexcept {
			bool complete = true;
			for (auto const& command_buffer : token->command_buffers) {
				auto status = command_buffer->status();
				if (status == MTL::CommandBufferStatusError) {
					if (!token->error) {
						try {
							token->error = std::make_exception_ptr(
								std::runtime_error(
									std::format(
										"Metal command buffer failed: {}",
										ErrorMessage(command_buffer->error())
									)
								)
							);
						}
						catch (...) {
							token->error = std::current_exception();
						}
					}
					continue;
				}
				if (status != MTL::CommandBufferStatusCompleted) {
					complete = false;
				}
			}
			return complete;
		}
	};

	template <>
	struct GetCompletionTokenError<metal::CompletionToken> {
		metal::CompletionToken* token;

		std::exception_ptr operator()() const noexcept {
			return token->error;
		}
	};

	template <>
	struct IsCompletionTokenStopped<metal::CompletionToken> {
		metal::CompletionToken* token;

		bool operator()() const noexcept {
			return token->stopped;
		}
	};

} // namespace fyuu_rhi::execution
#endif // defined(__APPLE__)
