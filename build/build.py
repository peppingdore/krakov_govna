import sys
import os
import time
import ascii_colors
from enum import Enum

import builder
import subprocess

from pathlib import Path



Library = builder.Library


class Build_Type(Enum):
	Debug         = 0
	Optimized     = 1
	Shipment      = 3


build_type = Build_Type.Debug



root_dir = os.path.normpath(os.path.join(os.path.abspath(os.path.dirname(__file__)), ".."))


def compile_shaders():
	shaders_src = os.path.join(root_dir, 'src/shaders')

	for filename in os.listdir(shaders_src):
		
		cmd_line = f'glslc {os.path.join(shaders_src, filename)} '


		path = Path(filename)
		if path.suffix == '.vert':
			cmd_line += ' -fshader-stage=vert'
		elif path.suffix == '.frag':
			cmd_line += ' -fshader-stage=frag'
		elif path.suffix == '.h':
			continue
		else:
			raise Exception('Shader must end with .vert or with .frag')

		output_file_name = f"{path}.spirv"
		output_path = os.path.join(root_dir, 'Runnable/shaders', output_file_name)

		cmd_line += f' -o {output_path} '


		source_compilation_start_time = time.perf_counter()
		run_result = subprocess.run(cmd_line, stdout = sys.stdout, stderr = subprocess.STDOUT, stdin = sys.stdin, shell = True)
		source_compilation_time = time.perf_counter() - source_compilation_start_time

		succeeded = run_result.returncode == 0

		file_name_background_rgb = (20, 150, 0) if succeeded else (200, 20, 0)

		file_name_to_print = filename
		
		if not succeeded:
			file_name_to_print += " (FAILED)    "
		else:
			file_name_to_print += f" (SUCCEEDED) -> {output_path}"

		file_name_to_print += "  {:.2f} s ".format(source_compilation_time)

		print(f'{ascii_colors.rgb(*file_name_background_rgb).background}{ascii_colors.rgb(0, 0, 0).foreground}--- {file_name_to_print}{ascii_colors.reset_background_color}{ascii_colors.reset_foreground_color}')
		

		if not succeeded:
			raise Exception(f'\n{ascii_colors.bright_red}Stopping further compilation{ascii_colors.reset_foreground_color}')



def main():

	global build_type

	if len(sys.argv) > 1:
		build_type_name_lower = sys.argv[1].lower()

		found_enum_value = False

		for it in Build_Type:
			if build_type_name_lower == it.name.lower():
				build_type = it
				found_enum_value = True

		if not found_enum_value:
			raise Exception(f'{ascii_colors.red}Build type "{sys.argv[1]}" is not found{ascii_colors.reset_foreground_color}')




	print(f'Build {build_type.name} started')
	build_start_time = time.perf_counter()


	compile_shaders()
	

	build_options = builder.Build_Options()

	build_options.generate_debug_symbols = True

	build_options.disable_warnings = True

	build_options.src_directory = 'src'



	if os.name == 'nt':
		# Ignored if clang-cl
		build_options.architecture = 'x86_64'
		build_options.vendor = 'pc'
		build_options.system = 'win32'
		build_options.abi = 'msvc'

		build_options.executable_path = 'Runnable/Typerminal.exe'

	else:
		build_options.architecture = 'x86_64'
		build_options.vendor = 'pc'
		build_options.system = 'linux'
		build_options.abi = 'gnu'

		build_options.executable_path = 'Runnable/Typerminal.elf'




	if 'unity' in sys.argv:
		build_options.sources = [ 'Unity_Build.cpp']
	else:
		build_options.sources = [
			'Main.cpp',
			'UI.cpp',
			'Renderer.cpp',
			'Renderer_Vulkan.cpp',
			'Video_Memory_Allocator.cpp',
			'Settings.cpp',
			'Python_Debugger.cpp',
			'Python_Interp.cpp',
			'Input.cpp',
			'Output_Processor.cpp',
			'Input_Processor.cpp',
			'Terminal_IO.cpp',
			'Terminal.cpp',
			'Typer_UI.cpp',
			'Key_Bindings.cpp',
		]


	build_options.include_directories = [
		'b_lib/ft',
	]

	if os.name == 'nt':
		build_options.include_directories.append('b_lib/icu')


	if os.name == 'posix':
		build_options.include_directories.append('python_headers')



	if os.name == 'nt':
		build_options.lib_directories = [
			'lib/windows'
		]
	elif os.name == 'posix':
		build_options.lib_directories = [
			'lib/linux',
			'src/b_lib/clip/build' # For libclip.a
		]

		build_options.libraries = [
			Library('X11'),
			Library('vulkan'),
			Library('pthread'),
			Library('libpython3.8.a', link_statically = True),
			Library('dl'),
			Library('libutil.so', link_statically = True),
			Library('libfreetype.so', link_statically = True),
			Library('libuuid.so', link_statically = True),

			Library('icuuc'),
			Library('z'),
			Library('ssl'),
			Library('crypto'),
			Library('crypt'),
			Library('nsl'),
			Library('curses'),
			Library('panel'),
			Library('xcb'),

			Library('libclip.a', link_statically = True)
		]

		build_options.additional_linker_flags.append("-rdynamic")
		build_options.additional_clang_flags.append("-fPIC")




	build_options.defines = [
		'_UNICODE',
		'UNICODE',
	]


	build_options.additional_clang_flags += [
		"-Wno-c++11-narrowing"
	]


	build_options.natvis_files = [
		"b_lib/b_lib.natvis",
	]


	build_options.print_source_compilation_time = True


	if os.name == 'nt':
		build_options.use_clang_cl = True

		build_options.use_windows_dynamic_crt = True
		build_options.use_windows_crt_debug_version = build_type != Build_Type.Shipment

		build_options.use_windows_subsystem = True

		if 'msvc' in sys.argv:
			build_options.use_msvc = True


	build_options.avx = True

	build_options.root_dir = root_dir
	build_options.intermidiate_dir = "build_temp"


	if build_type == Build_Type.Shipment:
		build_options.defines.append('NDEBUG')
	else:
		build_options.defines.append('_DEBUG')
		build_options.defines.append(('DEBUG', 1))


	if build_type == Build_Type.Debug:
		build_options.optimization_level = 0
	elif build_type == Build_Type.Optimized:
		build_options.optimization_level = 2
	elif build_type == Build_Type.Shipment:
		build_options.optimization_level = 3



	if 'tracy' in sys.argv:
		build_options.sources.append("tracy/TracyClient.cpp")
		build_options.defines.append('TRACY_ENABLE')

	if 'sanitize_address' in sys.argv:
		build_options.additional_clang_flags.append("-fsanitize=address")
		build_options.additional_linker_flags.append("-fsanitize=address")

		build_options.additional_clang_flags.append("-fno-omit-frame-pointer")
		build_options.additional_linker_flags.append("-fno-omit-frame-pointer")
		print(f'{ascii_colors.yellow.background}-fsanitize=address{ascii_colors.reset_background_color}')

	if 'sanitize_thread' in sys.argv:
		build_options.additional_clang_flags.append("-fsanitize=thread")
		build_options.additional_linker_flags.append("-fsanitize=thread")
		
		print(f'{ascii_colors.yellow.background}-fsanitize=thread{ascii_colors.reset_background_color}')




	result = builder.build(build_options)
	if result.success:
		icon_success = False
		try:
			icon_success = subprocess.run(f'rcedit "{result.executable_path}" --set-icon "{os.path.join(os.path.dirname(__file__), "../exe_icon.ico")}"', shell = True).returncode == 0
		except Exception as e:
			print('Setting icon exception:')
			print(e)
			print('\n')

		print(f'{(ascii_colors.green if icon_success else ascii_colors.red).foreground}Setting icon for executable {"SUCCEEDED" if icon_success else "FAILED"}{ascii_colors.reset_foreground_color}')




	print_str = f'Build took: {time.perf_counter() - build_start_time} seconds'

	print(print_str)


if __name__ == '__main__':
	main()