APP_OUT := out/switch/nx-discord-rpc
BOREALIS := app/lib/borealis/CMakeLists.txt
BOREALIS_GLFW := app/lib/borealis/library/lib/extern/glfw/CMakeLists.txt

test:
	cmake -S tests -B build/tests && cmake --build build/tests && ctest --test-dir build/tests --output-on-failure

sysmodule:
	$(MAKE) -C sysmodule

$(BOREALIS):
	git submodule update --init app/lib/borealis

$(BOREALIS_GLFW): $(BOREALIS)
	git -C app/lib/borealis submodule update --init library/lib/extern/glfw

app: $(BOREALIS)
	@test -n "$(DEVKITPRO)" || { echo "DEVKITPRO is not set: run 'nix develop -c make app'" >&2; exit 1; }
	cmake -S app -B build/app -DPLATFORM_SWITCH=ON -DUSE_DEKO3D=ON -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_TOOLCHAIN_FILE=$(DEVKITPRO)/cmake/Switch.cmake
	cmake --build build/app --parallel
	mkdir -p $(APP_OUT)
	cp build/app/nx-discord-rpc.nro $(APP_OUT)/nx-discord-rpc.nro

app-desktop: $(BOREALIS_GLFW)
	cmake -S app -B build/app-desktop -DPLATFORM_DESKTOP=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
	cmake --build build/app-desktop --parallel

dist: sysmodule app

compile_commands:
	cmake -S tests -B build/tests
	bear --output compile_commands.json -- $(MAKE) -B -C sysmodule
	bear --append --output compile_commands.json -- cmake --build build/tests --clean-first

clean:
	rm -rf build out compile_commands.json

.PHONY: test sysmodule app app-desktop dist compile_commands clean
