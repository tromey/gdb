#source: sh-link-abs.s
#readelf: -S

#...
 +\[ *[0-9]+\] +__patchable_\[\.\.\.\] +PROGBITS +[0-9a-f]+ +[0-9a-f]+
 +0+[248] +0+ +WAL +ABS +0 +1
#pass
