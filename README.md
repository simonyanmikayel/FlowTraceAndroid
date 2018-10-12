# Flow traces for android applications
This tool allows you to see logs and call tree (flow) of your application like this:

<img src="https://github.com/simonyanmikayel/FlowTrace/blob/master/Out/Help/flowtraces.png" width="350" height="290">

Tool based on [Proguard](https://www.guardsquare.com/en/products/proguard) and injects traces on every function entry in your android application. 
1.  Download [flowtraceinjector.jar flowtrce-rules.pro libflowtrace.so](https://github.com/simonyanmikayel/FlowTraceAndroid/blob/master/bin), and [FlowTrace.exe](https://github.com/simonyanmikayel/FlowTrace/blob/master/Out/Release/x64).
>Note: you can build all of them by cloning this repository and [FlowTrace](https://github.com/simonyanmikayel/FlowTrace).
2. Under your android project root directory create a directory named **flowtraces**
3. Copy **flowtraceinjector.jar** and **flowtrce-rules.pro** files into **flowtraces** directory
4. In top level **build.gradle** file add following:
```
buildscript {
    repositories {
        flatDir dirs: 'flowtraces'
    }
    dependencies {
        classpath 'net.sf.proguard:flowtraceinjector:'
    }
}
```
5. In application module **build.gradle** file add:
```
       debug {
            minifyEnabled true
            proguardFiles "${project.getRootProject().getRootDir()}/flowtraces/flowtrce-rules.pro"
        }
```
6. Copy **libflowtrace.so** file under **src\main\jniLibs\armeabi-v7a** directory of your application module
7. in your **AndroidManifest.xml** outside of application tag add:
```
<uses-permission android:name="android.permission.INTERNET" /> 
```
8. In your android smartphone or android devise simulator under **/data/data** folder create **flowtrace.ini** file.
>Note: if you have not permission to **/data/data** folder then create **flowtrace.ini** file under **/data/data/<you_app_name>** folder. 
9. In **flowtrace.ini** file write IP address of your pc and port number used by FlowTrace.exe application (default is 8888).  For example:
```
   ip=10.1.2.3
   port=8888
```
10. Run **FlowTrace.exe.** 
11. Build and install your application.
