module fyuu_desktop:entry;

import fyuu_desktop;
import fyuu_engine;

namespace fyuu_desktop {

	void Run(
		Descriptor const& descriptor,
		fyuu_engine::Logger& logger,
		EventSink& event_sink,
		fyuu_engine::Application& application
	) {
		Platform platform{ descriptor, event_sink };
		fyuu_engine::Runtime runtime{ platform, logger, application };
		runtime.Run();
	}

}
