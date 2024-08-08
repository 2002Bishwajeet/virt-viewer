project(
  'virt-viewer','c',
  version: '11.0',
  license: 'GPLv3+',
  default_options: [
    'buildtype=debugoptimized',
    'b_pie=true',
    'c_std=gnu99',
    'warning_level=1',
  ],
  meson_version: '>= 0.56.0'
)

pod2man = find_program('pod2man')

cc = meson.get_compiler('c')
python3 = find_program('python3', required: true)
# Python3 < 3.7 treats the C locale as 7-bit only. We must force env vars so
# it treats it as UTF-8 regardless of the user's locale.
runutf8 = [ 'LC_ALL=', 'LANG=C', 'LC_CTYPE=en_US.UTF-8' ]
git = run_command('test', '-d', '.git', check: false).returncode() == 0

conf_data = configuration_data()
conf_data.set('VERSION', meson.project_version())
conf_data.set('PACKAGE_STRING', 'virt-viewer ' + meson.project_version())

# Keep these two definitions in agreement.
glib_min_version='>=2.48'
glib_min_version_symbol='GLIB_VERSION_2_48'

# Keep these two definitions in agreement.
gtk_min_version='>=3.18'
gtk_min_version_symbol='GDK_VERSION_3_18'

libxml_min_version='>=2.6.0'
libvirt_min_version='>=1.2.8'
libvirt_glib_min_version='>=0.1.8'
gtk_vnc_min_version='>=0.4.0'
spice_gtk_min_version='>=0.35'
spice_protocol_min_version='>=0.12.7'
govirt_min_version='>=0.3.7'
govirt_max_version='>=0.3.9'
rest_min_version='>=0.8'
vte_min_version='>=0.46.0'
bash_completion_version='2.0'

add_global_arguments('-DGLIB_VERSION_MIN_REQUIRED=@0@'.format(glib_min_version_symbol), language: 'c')
add_global_arguments('-DGLIB_VERSION_MAX_ALLOWED=@0@'.format(glib_min_version_symbol), language: 'c')

add_global_arguments('-DGDK_VERSION_MIN_REQUIRED=@0@'.format(gtk_min_version_symbol), language: 'c')
add_global_arguments('-DGDK_VERSION_MAX_ALLOWED=@0@'.format(gtk_min_version_symbol), language: 'c')

cc_flags = []

git_werror = get_option('git_werror')
if git_werror.enabled() or git_werror.auto() and git
  cc_flags += [ '-Werror' ]
endif

cc_flags += [
  '-fno-common',
  '-W',
  '-Wabsolute-value',
  '-Waddress',
  '-Waddress-of-packed-member',
  '-Waggressive-loop-optimizations',
  '-Wall',
  '-Wattribute-warning',
  '-Wattributes',
  '-Wbool-compare',
  '-Wbool-operation',
  '-Wbuiltin-declaration-mismatch',
  '-Wbuiltin-macro-redefined',
  '-Wcannot-profile',
  '-Wcast-align',
  '-Wcast-align=strict',
  '-Wcast-function-type',
  '-Wchar-subscripts',
  '-Wclobbered',
  '-Wcomment',
  '-Wcomments',
  '-Wcoverage-mismatch',
  '-Wcpp',
  '-Wdangling-else',
  '-Wdate-time',
  '-Wdeprecated-declarations',
  '-Wdesignated-init',
  '-Wdouble-promotion',
  '-Wdiscarded-array-qualifiers',
  '-Wdiscarded-qualifiers',
  '-Wdiv-by-zero',
  '-Wduplicated-cond',
  '-Wduplicate-decl-specifier',
  '-Wempty-body',
  '-Wendif-labels',
  '-Wexpansion-to-defined',
  '-Wextra',
  '-Wformat-contains-nul',
  '-Wformat-extra-args',
  '-Wformat-nonliteral',
  '-Wformat-security',
  '-Wformat-y2k',
  '-Wformat-zero-length',
  '-Wframe-address',
  '-Wfree-nonheap-object',
  '-Whsa',
  '-Wif-not-aligned',
  '-Wignored-attributes',
  '-Wignored-qualifiers',
  '-Wimplicit',
  '-Wimplicit-function-declaration',
  '-Wimplicit-int',
  '-Wincompatible-pointer-types',
  '-Winit-self',
  '-Winline',
  '-Wint-conversion',
  '-Wint-in-bool-context',
  '-Wint-to-pointer-cast',
  '-Winvalid-memory-model',
  '-Winvalid-pch',
  '-Wlogical-not-parentheses',
  '-Wlogical-op',
  '-Wmain',
  '-Wmaybe-uninitialized',
  '-Wmemset-elt-size',
  '-Wmemset-transposed-args',
  '-Wmisleading-indentation',
  '-Wmissing-attributes',
  '-Wmissing-braces',
  '-Wmissing-declarations',
  '-Wmissing-field-initializers',
  '-Wmissing-include-dirs',
  '-Wmissing-parameter-type',
  '-Wmissing-profile',
  '-Wmissing-prototypes',
  '-Wmultichar',
  '-Wmultistatement-macros',
  '-Wnarrowing',
  '-Wnested-externs',
  '-Wnonnull',
  '-Wnonnull-compare',
  '-Wnull-dereference',
  '-Wodr',
  '-Wold-style-declaration',
  '-Wold-style-definition',
  '-Wopenmp-simd',
  '-Woverflow',
  '-Woverride-init',
  '-Wpacked-bitfield-compat',
  '-Wpacked-not-aligned',
  '-Wparentheses',
  '-Wpointer-arith',
  '-Wpointer-compare',
  '-Wpointer-sign',
  '-Wpointer-to-int-cast',
  '-Wpragmas',
  '-Wpsabi',
  '-Wrestrict',
  '-Wreturn-local-addr',
  '-Wreturn-type',
  '-Wscalar-storage-order',
  '-Wsequence-point',
  '-Wshadow',
  '-Wshift-count-negative',
  '-Wshift-count-overflow',
  '-Wshift-negative-value',
  '-Wsizeof-array-argument',
  '-Wsizeof-pointer-div',
  '-Wsizeof-pointer-memaccess',
  '-Wstrict-aliasing',
  '-Wstrict-prototypes',
  '-Wstringop-truncation',
  '-Wsuggest-attribute=cold',
  '-Wsuggest-attribute=const',
  '-Wsuggest-attribute=format',
  '-Wsuggest-attribute=noreturn',
  '-Wsuggest-attribute=pure',
  '-Wsuggest-final-methods',
  '-Wsuggest-final-types',
  '-Wswitch',
  '-Wswitch-bool',
  '-Wswitch-unreachable',
  '-Wsync-nand',
  '-Wtautological-compare',
  '-Wtrampolines',
  '-Wtrigraphs',
  '-Wtype-limits',
  '-Wuninitialized',
  '-Wunknown-pragmas',
  '-Wunused',
  '-Wunused-but-set-parameter',
  '-Wunused-but-set-variable',
  '-Wunused-function',
  '-Wunused-label',
  '-Wunused-local-typedefs',
  '-Wunused-parameter',
  '-Wunused-result',
  '-Wunused-value',
  '-Wunused-variable',
  '-Wvarargs',
  '-Wvariadic-macros',
  '-Wvector-operation-performance',
  '-Wvla',
  '-Wvolatile-register-var',
  '-Wwrite-strings',
]

# gcc --help=warnings outputs
ptrdiff_max = cc.sizeof('ptrdiff_t', prefix: '#include <stddef.h>')
size_max = cc.sizeof('size_t', prefix: '#include <stdint.h>')
# Compute max safe object size by checking ptrdiff_t and size_t sizes.
# Ideally we would get PTRDIFF_MAX and SIZE_MAX values but it would
# give us (2147483647L) and we would have to remove the () and the suffix
# in order to convert it to numbers to be able to pick the smaller one.
alloc_max = run_command(
  'python3', '-c',
  'print(min(2**(@0@ * 8 - 1) - 1, 2**(@1@ * 8) - 1))'.format(ptrdiff_max, size_max),
  check: true,
)
cc_flags += [
  '-Walloc-size-larger-than=@0@'.format(alloc_max.stdout().strip()),
  '-Warray-bounds=2',
  '-Wattribute-alias=2',
  '-Wformat-overflow=2',
  '-Wformat-truncation=2',
  '-Wimplicit-fallthrough=5',
  '-Wnormalized=nfc',
  '-Wshift-overflow=2',
  '-Wstringop-overflow=2',
  #'-Wunused-const-variable=2',
  '-Wvla-larger-then=4031',
]

cc_flags += [
  # So we have -W enabled, and then have to explicitly turn off...
  '-Wno-sign-compare',

  # We do "bad" function casts all the time for event callbacks
  '-Wno-cast-function-type',

  # Clang incorrectly complains about dup typedefs win gnu99 mode
  # so use this Clang-specific arg to keep it quiet
  '-Wno-typedef-redefinition',

  # We don't use -Wc++-compat so we have to enable it explicitly
  '-Wjump-misses-init',

  # -Wswitch is enabled but that doesn't report missing enums if a default:
  # is present
  '-Wswitch-enum',

  # -Wformat=2 implies -Wformat-nonliteral so we need to manually exclude it
  '-Wno-format-nonliteral',

  # -Wformat enables this by default, and we should keep it,
  # but need to rewrite various areas of code first
  '-Wno-format-truncation',

  # This should be < 256 really. Currently we're down to 4096,
  # but using 1024 bytes sized buffers (mostly for virStrerror)
  # stops us from going down further
  '-Wframe-larger-than=4096',

  # extra special flags
  '-fexceptions',
  '-fasynchronous-unwind-tables',

  # Need -fipa-pure-const in order to make -Wsuggest-attribute=pure
  # fire even without -O.
  '-fipa-pure-const',

  # We should eventually enable this, but right now there are at
  # least 75 functions triggering warnings.
  '-Wno-suggest-attribute=pure',
  '-Wno-suggest-attribute=const',
]

# on aarch64 error: -fstack-protector not supported for this target
if host_machine.cpu_family() != 'aarch64'
  if host_machine.system() in [ 'linux', 'freebsd', 'windows' ]
    # we prefer -fstack-protector-strong but fallback to -fstack-protector-all
    fstack_cflags = cc.first_supported_argument([
      '-fstack-protector-strong',
      '-fstack-protector-all',
    ])
    cc_flags += fstack_cflags

    # When building with mingw using -fstack-protector requires libssp library
    # which is included by using -fstack-protector with linker.
    if fstack_cflags.length() == 1 and host_machine.system() == 'windows'
      add_project_link_arguments(fstack_cflags, language: 'c')
    endif
  endif
endif

# Clang complains about unused static inline functions which are common
# with G_DEFINE_AUTOPTR_CLEANUP_FUNC.
w_unused_function_args = ['-Wunused-function', '-Werror']
w_unused_function_code = '''
  static inline void foo(void) {}

  int main(void) { return 0; }
'''
# -Wunused-function is implied by -Wall, we must turn it off explicitly.
if not cc.compiles(w_unused_function_code, args: w_unused_function_args)
  cc_flags += ['-Wno-unused-function']
endif

cc_flags_disabled = [
  # In meson this is specified using 'c_std=gnu99' in project() function.
  '-std=gnu99',

  # don't care about C++ compiler compat
  '-Wc++-compat',
  '-Wabi',
  '-Wdeprecated',

  # Don't care about ancient C standard compat
  '-Wtraditional',
  '-Wtraditional-conversion',

  # Ignore warnings in /usr/include
  '-Wsystem-headers',

  # Happy for compiler to add struct padding
  '-Wpadded',

  # GCC very confused with -O2
  '-Wunreachable-code',

  # Too many to deal with
  '-Wconversion',
  '-Wsign-conversion',

  # Need to allow bad cast for execve()
  '-Wcast-qual',

  # We need to use long long in many places
  '-Wlong-long',

  # We allow manual list of all enum cases without default
  '-Wswitch-default',

  # Not a problem since we don't use -fstrict-overflow
  '-Wstrict-overflow',

  # Not a problem since we don't use -funsafe-loop-optimizations
  '-Wunsafe-loop-optimizations',

  # gcc 4.4.6 complains this is C++ only; gcc 4.7.0 implies this from -Wall
  '-Wenum-compare',

  # gcc 5.1 -Wformat-signedness mishandles enums, not ready for prime time
  '-Wformat-signedness',

  # Several conditionals expand the same on both branches depending on the
  # particular platform/architecture
  '-Wduplicated-branches',

  # > This warning does not generally indicate that there is anything wrong
  # > with your code; it merely indicates that GCC's optimizers are unable
  # > to handle the code effectively.
  # Source: https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html
  '-Wdisabled-optimization',

  # Various valid glib APIs/macros trigger this warning
  '-Wbad-function-cast',

  # We might fundamentally need some of these disabled forever, but
  # ideally we'd turn many of them on
  '-Wfloat-equal',
  '-Wpacked',
  '-Wunused-macros',
  '-Woverlength-strings',
  '-Wstack-protector',
  '-Wsuggest-attribute=malloc',

  # g_return_val_if_fail usage violates this often
  '-Wdeclaration-after-statement',
]

foreach flag : cc_flags_disabled
  if cc_flags.contains(flag)
    error('@0@ is disabled but listed in cc_flags'.format(flag))
  endif
endforeach

supported_cc_flags = cc.get_supported_arguments(cc_flags)
add_project_arguments(supported_cc_flags, language: 'c')


prefix = get_option('prefix')
localedir = join_paths(prefix, get_option('localedir'))
datadir = join_paths(prefix, get_option('datadir'))
mandir = join_paths(prefix, get_option('mandir'))

add_global_arguments('-DLOCALE_DIR="@0@"'.format(localedir), language: 'c')

libm_dep = cc.find_library('m', required : true)

glib_dep = dependency('glib-2.0', version: glib_min_version)
gmodule_dep = dependency('gmodule-2.0', version: glib_min_version)
gtk_dep = dependency('gtk+-3.0', version: gtk_min_version)

libxml_dep = dependency('libxml-2.0', version: libxml_min_version)

libvirt_dep = dependency('libvirt', version: libvirt_min_version, required: get_option('libvirt'))
libvirt_glib_dep = dependency('libvirt-glib-1.0', version: libvirt_glib_min_version, required: get_option('libvirt'))
if get_option('libvirt').auto()
  if libvirt_dep.found() and not libvirt_glib_dep.found()
    libvirt_dep = dependency('', required: false)
  endif
  if not libvirt_dep.found() and libvirt_glib_dep.found()
    libvirt_glib_dep = dependency('', required: false)
  endif
endif
if libvirt_dep.found()
  conf_data.set('HAVE_LIBVIRT', '1')
endif

gtk_vnc_dep = dependency('gtk-vnc-2.0', version: gtk_vnc_min_version, required: get_option('vnc'))
if gtk_vnc_dep.found()
  conf_data.set('HAVE_GTK_VNC', '1')
endif

spice_glib_dep = dependency('spice-client-glib-2.0', version: spice_gtk_min_version, required: get_option('spice'))
spice_gtk_dep = dependency('spice-client-gtk-3.0', version: spice_gtk_min_version, required: get_option('spice'))
spice_protocol_dep = dependency('spice-protocol', version: spice_protocol_min_version, required: get_option('spice'))
if get_option('spice').auto()
  if not spice_protocol_dep.found() or not spice_gtk_dep.found() or not spice_glib_dep.found()
    spice_protocol_dep = dependency('', required: false)
    spice_glib_dep = dependency('', required: false)
    spice_gtk_dep = dependency('', required: false)
  endif
endif
if spice_gtk_dep.found()
  conf_data.set('HAVE_SPICE_GTK', '1')
endif

govirt_dep = dependency('govirt-1.0', version: govirt_min_version, required: get_option('ovirt'))
rest_dep = dependency('rest-0.7', version: rest_min_version, required: get_option('ovirt'))
if get_option('ovirt').auto()
  if govirt_dep.found() and not rest_dep.found()
    govirt_dep = dependency('', required: false)
  endif
  if not govirt_dep.found() and rest_dep.found()
    rest_dep = dependency('', required: false)
  endif
endif
if govirt_dep.found()
  new_govirt_dep = dependency('govirt-1.0', version: govirt_max_version, required: false)
  if new_govirt_dep.found()
    if get_option('ovirt').auto()
      govirt_dep = dependency('', required: false)
    else
      error('libgovirt >= 0.3.9 (with librest-1.0) not supported')
    endif
  endif
endif
if govirt_dep.found()
  if cc.has_function('ovirt_storage_domain_get_disks', dependencies: govirt_dep)
    conf_data.set('HAVE_OVIRT_STORAGE_DOMAIN_GET_DISKS', '1')
  endif
  conf_data.set('HAVE_OVIRT', '1')
endif

vte_dep = dependency('vte-2.91', version: vte_min_version, required: get_option('vte'))
if vte_dep.found()
  conf_data.set('HAVE_VTE', '1')
endif

bash_completion_dep = dependency('bash-completion', version: '>=' + bash_completion_version, required: get_option('bash_completion'))

if bash_completion_dep.found()
  bash_completion_dir = get_option('bash_completion_dir')
  if bash_completion_dir == ''
    bash_completion_dir = bash_completion_dep.get_variable(pkgconfig: 'completionsdir')
    bash_completion_prefix = bash_completion_dep.get_variable(pkgconfig: 'prefix')
    rc = run_command(
      'python3', '-c',
      'print("@0@".replace("@1@", "@2@"))'.format(
        bash_completion_dir, bash_completion_prefix, prefix,
      ),
      check: true,
    )
    bash_completion_dir = rc.stdout().strip()
  endif
endif

id = get_option('build-id')
if id != ''
  conf_data.set('BUILDID', '-@0@'.format(id))
else
  conf_data.set('BUILDID', '')
endif
osid = get_option('os-id')
if osid != ''
  conf_data.set('REMOTE_VIEWER_OS_ID', '"' + osid + '"')
endif

arr_version = meson.project_version().split('.')

conf_data.set('WINDOWS_PRODUCTVERSION', '@0@.@1@.@2@'.format(
  arr_version[0],
  arr_version[1],
  get_option('build-id')))

if host_machine.system() == 'windows'
  if host_machine.cpu() != 'x86_64'
    wixl_arch = 'x86'
  else
    wixl_arch = 'x64'
  endif
  conf_data.set('WIXL_ARCH', wixl_arch)
endif

configure_file(
  input: 'config.h.in',
  output: 'config.h',
  configuration: conf_data
)

subdir('build-aux')

if git
  authors = run_command(python3.full_path(), gen_authors_prog.full_path(), env: runutf8, check: true)
  authors_file = 'AUTHORS.in'

  authors_conf = configuration_data()
  authors_conf.set('authorslist', authors.stdout())

  configure_file(
    input: authors_file,
    output: '@BASENAME@',
    configuration: authors_conf,
  )

  configure_file(
    input: 'virt-viewer.spec.in',
    output: 'virt-viewer.spec',
    configuration: conf_data
  )

  configure_file(
    input: 'mingw-virt-viewer.spec.in',
    output: 'mingw-virt-viewer.spec',
    configuration: conf_data
  )

  meson.add_dist_script(python3.full_path(), dist_prog.full_path(), meson.project_build_root(), 'AUTHORS')
  meson.add_dist_script(python3.full_path(), dist_prog.full_path(), meson.project_build_root(), 'virt-viewer.spec')
endif

gnome = import('gnome')
i18n = import('i18n')

i18n_itsdir = join_paths(meson.project_source_root(), 'data', 'gettext')
top_include_dir = [include_directories('.')]

update_mime_database = find_program('update-mime-database', required: false)
update_icon_cache = find_program('gtk-update-icon-cache', required: false)
update_desktop_database = find_program('update-desktop-database', required: false)

update_mime_database_path = ''
if update_mime_database.found()
  update_mime_database_path = update_mime_database.full_path()
endif

update_icon_cache_path = ''
if update_icon_cache.found()
  update_icon_cache_path = update_icon_cache.full_path()
endif

update_desktop_database_path = ''
if update_desktop_database.found()
  update_desktop_database_path = update_desktop_database.full_path()
endif

meson.add_install_script('build-aux/post_install.py',
                         update_mime_database_path,
                         update_icon_cache_path,
                         update_desktop_database_path)

subdir('icons')
subdir('src')
subdir('po')
subdir('man')
subdir('tests')
if bash_completion_dep.found()
  subdir('bash-completion')
endif
subdir('data')
