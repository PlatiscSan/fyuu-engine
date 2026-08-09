export module plastic.disable_copy;
namespace plastic::utility {

    export class DisableCopy {
    public:
        DisableCopy() = default;

        DisableCopy(DisableCopy const&) = delete;
        DisableCopy& operator=(DisableCopy const&) = delete;

        DisableCopy(DisableCopy&&) noexcept = default;
        DisableCopy& operator=(DisableCopy&&) noexcept = default;

        ~DisableCopy() noexcept = default;
    };

}