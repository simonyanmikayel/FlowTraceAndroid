#-verbose
-dontwarn
-dontshrink
-dontoptimize
-dontpreverify
-dontobfuscate

#uncomment next line to disable flow trace injection
#-dontinject

#comment or modify next line to have more or less traces
-flowtracesfilter !android/**, !java/**
