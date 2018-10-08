#-injars 'C:\flowtrace\trace_injector\out\production\test'(**.class)
-injars 'c:\flowtrace\FlowTraceAndroid\testapplication\build\intermediates\classes\debug\com\example\testapplication'(MainActivity.class)
-outjars 'c:\flowtrace\FlowTraceAndroid\flowtraceinjector\build\tmp'

-verbose
-dontwarn
-dontshrink
-dontoptimize
-dontpreverify
-dontobfuscate

#-dontinject
-flowtracesfilter !android/**, !java/**

-keep class *.* {

}
