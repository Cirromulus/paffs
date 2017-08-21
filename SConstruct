import os

AddOption(
    '--release-build',
    action='store_true',
    help='release build',
    default=False)
    
AddOption(
	'--with-clang',
	action='store_true',
	help='clang build',
	default=False)
	
AddOption(
	'--msan',
	action='store_true',
	help='clang build',
	default=False)
	
AddOption(
	'--usan',
	action='store_true',
	help='clang build',
	default=False)
	
AddOption(
	'--asan',
	action='store_true',
	help='clang build',
	default=False)

print("PAFFS")

rootpath = Dir('.').abspath
extpath = Dir('..').abspath

envGlobal = Environment(toolpath=[os.path.join(extpath, 'scons-build-tools/site_tools')],
                        tools=['utils_buildformat'],
                        BASEPATH=os.path.abspath('.'),
                        ENV=os.environ)

if GetOption('release_build'):
	buildfolder = os.path.join(rootpath, 'release')
	envGlobal['CCFLAGS_optimize'] = ['-O2']
else:
	buildfolder = os.path.join(rootpath, 'debug')
	envGlobal.ParseFlags('-DDEBUG')
	envGlobal['CCFLAGS_optimize'] = ['-O0']

if GetOption('with_clang'):
	envGlobal.Tool('compiler_hosted_llvm')
	buildfolder += "_llvm"
else:
	envGlobal.Tool('compiler_hosted_gcc')
	
envGlobal.Tool('settings_buildpath')
envGlobal['BUILDPATH'] = os.path.abspath(buildfolder)
envGlobal['ROOTPATH'] = rootpath
envGlobal['OUTPOST_CORE_PATH'] = os.path.join(extpath, 'outpost-core')
envGlobal['SATFON_SIMULATION_PATH'] = os.path.join(extpath, 'outpost-core')

envGlobal['CXXFLAGS_language'] = ['-std=c++11']

envGlobal.Append(CPPPATH=[
	rootpath,
])

envGlobal['PAFFSCONFIG'] = 'flashSimu'

clangcflags = [
	#'-v',
	'-fno-omit-frame-pointer',
	'-fno-optimize-sibling-calls',
]

if GetOption('with_clang'):
	if GetOption('msan'):
		clangcflags.extend([
			'-fsanitize=memory',
			'-stdlib=libc++',
		])
	elif GetOption('usan'):
		clangcflags.extend([
			'-fsanitize=undefined',
		])
	elif GetOption('asan'):
		clangcflags.extend([
			'-fsanitize=address',
		])
	
clangcxflags.extend(clangcflags)
	

if GetOption('with_clang'):
	envGlobal.Append(CCFLAGS_target = clangcflags)
	envGlobal.Append(CXXFLAGS_target = clangcxflags)
	envGlobal.Append(LINKFLAGS_target = clanglflags)

envGlobal.SConscript([
		os.path.join(rootpath, 'src/driver/SConscript.nulldriver'),			#Modify Driver Sconscript here
	],
exports=['envGlobal'])

env = envGlobal.Clone()

files = [os.path.abspath('main.cpp')]

env.Append(LIBS=[
	'nullDriver'															#Modify Driver Lib here
])

if GetOption('with_clang') and  GetOption('msan'):
	env.Append(LIBS=[
		'c++abi',
		'c++',
	])	

envGlobal.Alias('build', env.Program('satfon', files))
envGlobal.Default('build', 'flashView')
