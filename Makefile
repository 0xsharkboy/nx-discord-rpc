test:
	cmake -S tests -B build/tests && cmake --build build/tests && ctest --test-dir build/tests --output-on-failure

sysmodule:
	$(MAKE) -C sysmodule

compile_commands:
	cmake -S tests -B build/tests
	bear --output compile_commands.json -- $(MAKE) -B -C sysmodule
	bear --append --output compile_commands.json -- cmake --build build/tests --clean-first

clean:
	rm -rf build out compile_commands.json

.PHONY: test sysmodule compile_commands clean
