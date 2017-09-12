org_kernel_kmod_sources = files('''
        main.c
'''.split())

org_kernel_kmod_varlink_c_inc = custom_target(
        'org.kernel.kmod.varlink',
        input : 'org.kernel.kmod.varlink',
        output : 'org.kernel.kmod.varlink.c.inc',
        command : [varlink_wrapper_py, '@INPUT@', '@OUTPUT@'])

exe = executable(
        'org.kernel.kmod',
        org_kernel_kmod_sources,
        org_kernel_kmod_varlink_c_inc,
        dependencies : [
                libvarlink,
                libkmod
        ],
        install : true)
