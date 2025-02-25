# name: EABI build attributes: some files missing Tag_Feature_BTI, but -z force-bti means that the output has Tag_Feature_BTI=0x1
# source: ba-aarch64-1-bti-1.s
# source: ba-aarch64-1-unknown-tag.s
# source: ba-aarch64-1-bti-via-gnu-props.s
# source: ba-aarch64-1-bti-2.s
# as: -defsym __property_bti__=1
# ld: -shared -T bti-plt.ld
#warning: \A[^\n]*ba-aarch64-1-unknown-tag\.o: warning: cannot merge unknown tag 'Tag_unknown_4' \(=0x1\) in subsection 'aeabi_feature_and_bits'\.
# readelf: --arch-specific

Subsections:
 - Name:[	 ]+aeabi_feature_and_bits
   Scope:[	 ]+public
   Length:[	 ]+35
   Comprehension:[	 ]+optional
   Encoding:[	 ]+ULEB128
   Values:
 +Tag_Feature_BTI:[	 ]+1 .*
 +Tag_Feature_PAC:[	 ]+0 .*
 +Tag_Feature_GCS:[	 ]+0 .*
