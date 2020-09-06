#-injars 'C:\work\_Repos\FlowTraceAndroid\trace_injector\out\production\test'(**.class)
#-injars 'c:\work\_Repos\FlowTraceAndroid\testapplication\build\intermediates\javac\debug\classes\com\example\testapplication\'(MainActivity.class)
-injars 'c:\work\_Repos\FlowTraceAndroid\testapplication\build\intermediates\javac\debug\classes\com\example\testapplication\'
-outjars 'C:\work\_Repos\FlowTraceAndroid\flowtraceinjector\build\tmp'

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
