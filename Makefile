APP_OUT := out/switch/nx-discord-rpc

test:
	cmake -S tests -B build/tests && cmake --build build/tests && ctest --test-dir build/tests --output-on-failure

sysmodule:
	$(MAKE) -C sysmodule

app:
	cmake -S app -B build/app -DPLATFORM_SWITCH=ON -DUSE_DEKO3D=ON -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_TOOLCHAIN_FILE=$(DEVKITPRO)/cmake/Switch.cmake
	cmake --build build/app --parallel
	mkdir -p $(APP_OUT)
	cp build/app/nx-discord-rpc.nro $(APP_OUT)/nx-discord-rpc.nro

app-desktop:
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
