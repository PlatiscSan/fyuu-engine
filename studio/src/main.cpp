import fyuu_desktop;
import fyuu_engine;

int main() {
	fyuu_desktop::Descriptor const descriptor{
		"Fyuu Studio",
		1600,
		900,
		true,
		true
	};
	fyuu_engine::Application application;
	try {
		fyuu_desktop::Run(descriptor, application);
		return 0;
	}
	catch (fyuu_engine::Error const&) {
		return 1;
	}
}
