project('usbaudio', 'c',
        version: '1.0',
        default_options: 'c_std=c11')

src = [
    'src/main.c',
    'src/aoa.c',
    'src/pulse.c',
]

dependencies = [
    dependency('libpulse'),
    dependency('libusb-1.0'),
]

src_dir = include_directories('src')

# -Db_ndebug requires meson >= 0.45, do it manually to support older versions
conf = configuration_data()
conf.set('NDEBUG', get_option('buildtype') != 'debug')
configure_file(configuration: conf, output: 'config.h')

executable('usbaudio', src,
           dependencies: dependencies,
           include_directories: src_dir,
           install: true)
