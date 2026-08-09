module fyuu_desktop:entry;

import fyuu_desktop;
import fyuu_engine;

namespace fyuu_desktop {

	void Run(
		Descriptor const& descriptor,
		fyuu_engine::Application& application
	) {
		Platform platform{ descriptor };
		fyuu_engine::Runtime runtime{ platform, application };
		runtime.Run();
	}

}
