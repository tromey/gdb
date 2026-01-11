#source: sh-link-common.s
#readelf: -t

#...
 +\[ *[0-9]+\] +__patchable_function_entries
 +PROGBITS +[0-9a-f]+ +[0-9a-f]+ +0+[248] +0+ +COM +0 +1
 +\[0+83\]: WRITE, ALLOC, LINK ORDER
#pass
