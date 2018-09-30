package com.example.testapplication;

import android.content.Context;
import android.support.test.InstrumentationRegistry;
import android.support.test.runner.AndroidJUnit4;
import android.util.Log;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.lang.reflect.Method;

import static org.junit.Assert.*;

//import android.os.Process;
//import dalvik.system.PathClassLoader;


/**
 * Instrumented test, which will execute on an Android device.
 *
 * @see <a href="http://d.android.com/tools/testing">Testing documentation</a>
 */
@RunWith(AndroidJUnit4.class)
public class ExampleInstrumentedTest {
    native int testJNI();
    @Test
    public void useAppContext() {
        // Context of the app under test.
        Context appContext = InstrumentationRegistry.getTargetContext();
//        Throwable
        //StackTraceElement[] stack = Thread.currentThread().getStackTrace();
//        try {
//            String str = new Object(){}.getClass().getEnclosingMethod().getName();
//            Method m = Throwable.class.getDeclaredMethod("getStackTraceElement",
//                    int.class);
//            m.setAccessible(true);
//        } catch (Exception e) {
//            e.printStackTrace();
//        }
        System.loadLibrary("flowtrace");
        testJNI();
        Log.d("Aaa", "Bbb");
        Log.e("eee", "www", new Throwable("uuu"));
        Log.d("111", "222");


        assertEquals("com.example.testapplication", appContext.getPackageName());
    }
}
