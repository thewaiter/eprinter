project('moksha-printers', 'c', version: '0.0.1')

deps = [
  dependency('elementary'),
  dependency('ecore'),
  dependency('eina'),
  dependency('cups'),   # if missing: meson uses cups-config; else link -lcups
]

executable('moksha-printers',
  ['main.c', 'cp_backend.c', 'dlg_add.c', 'dlg_props.c', 'notify.c', 'cupsevents.c'],
  c_args: ['-Wno-deprecated-declarations'],   # ppd.h is deprecated in CUPS 2.x
  dependencies: deps,
  install: true)
