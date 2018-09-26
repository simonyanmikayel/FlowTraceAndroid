package proguard.inject;

import java.io.PrintWriter;
import java.io.StringWriter;
import java.util.Arrays;

public class FlowTraceWriter {

    static boolean initialized = false;
    public static native int initTraces();
    public static native void FlowTraceLogFlow(String thisClassName, String thisMethodName, String callClassName, String callMethodName, int log_type, int thisID, int callID, int thisLineNumber, int callLineNumber);
    public static native void FlowTraceLogTrace(int severity, String thisClassName, String thisMethodName, int thisLineNumber, int callLineNumber, String tag, String msg, int flags);

    public static final int LOG_FATAL = 0;
    public static final int LOG_ERROR = 1;
    public static final int LOG_WARNING = 2;
    public static final int LOG_INFO = 3;
    public static final int LOG_DEBUG = 4;
    public static final int LOG_COMMON = 5;
    public static final int LOG_FLAG_JAVA = 2;
    public static final int LOG_FLAG_EXCEPTION = 4;
    public static final int LOG_FLAG_RUNNABLE_INIT = 8;
    public static final int LOG_FLAG_RUNNABLE_RUN = 16;


    static {
        System.loadLibrary("flowtrace");
        initialized = (0 != initTraces());
    }

    static public void logFlow(int log_type) {
        String thisClassName = "";
        String thisMethodName = "";
        String callClassName = "";
        String callMethodName = "";
        int thisLineNumber = -1;
        int callLineNumber = -1;

        int s1 = 3;
        int s2 = s1 + 1;
        StackTraceElement[] stack = Thread.currentThread().getStackTrace();
        if (stack.length <= s1) {
            //System.out.println("stack.length: " + stack.length);
            return;
        }
        thisClassName = stack[s1].getClassName();
        thisMethodName = stack[s1].getMethodName();
        thisLineNumber = stack[s1].getLineNumber();
        if (stack.length > s2) {
            callClassName = stack[s2].getClassName();
            callMethodName = stack[s2].getMethodName();
            callLineNumber = stack[s2].getLineNumber();
        }

        int thisID = thisClassName.hashCode() +  31 * thisMethodName.hashCode();;
        int callID = callClassName.hashCode() +  31 * callMethodName.hashCode();;

        //System.out.println((log_type == 0) ? " -> " : " <- ") + thisClassName + " " + thisMethodName + " "  + thisLineNumber + " " + callClassName + " " + callMethodName + " "  + callLineNumber);
        FlowTraceLogFlow(thisClassName, thisMethodName, callClassName, callMethodName, log_type, thisID, callID, thisLineNumber, callLineNumber);
    }

    static public void logRunnable(int runnableMethod, Object o)
    {
        String thisClassName = "";
        String thisMethodName = "";
        int callLineNumber = -1;
        int flags = LOG_FLAG_JAVA | (runnableMethod == 1 ? LOG_FLAG_RUNNABLE_INIT : LOG_FLAG_RUNNABLE_RUN);

        int s1 = 3;
        StackTraceElement[] stack = Thread.currentThread().getStackTrace();
        if (stack.length <= s1) {
            return;
        }
        thisClassName = stack[s1].getClassName();
        thisMethodName = stack[s1].getMethodName();
        callLineNumber = stack[s1].getLineNumber();

        //System.out.println("   ----------------->" + (runnableMethod == 1 ? " <init> " : " run ") + o.hashCode());
        FlowTraceLogTrace(LOG_DEBUG, thisClassName, thisMethodName, o.hashCode(), callLineNumber, "Runnable id " + o.hashCode(), runnableMethod == 1 ? "<init>" : "run", flags);
    }


    static public void logTrace(int severity, String tag, String msg, Throwable exception)
    {
        String thisClassName = "";
        String thisMethodName = "";
        int callLineNumber = -1;
        int flags = LOG_FLAG_JAVA;

        int s1 = 4;
        StackTraceElement[] stack = Thread.currentThread().getStackTrace();
        if (stack.length <= s1) {
            return;
        }
        thisClassName = stack[s1].getClassName();
        thisMethodName = stack[s1].getMethodName();
        callLineNumber = stack[s1].getLineNumber();

        if (exception != null)
        {
            flags |= LOG_FLAG_EXCEPTION;
            StringWriter writer = new StringWriter();
            PrintWriter printWriter= new PrintWriter(writer);
            exception.printStackTrace(printWriter);
            msg = msg + "\n" + writer.toString();
        }
        FlowTraceLogTrace(severity, thisClassName, thisMethodName, 0, callLineNumber, tag, msg, flags);
    }

    public static class MethodSignature
    {
        private String   name;
        private String[] parameters;


        public MethodSignature(String name, Class[] parameters)
        {
            this.name       = name;
            this.parameters = new String[parameters.length];
            for (int i = 0; i < parameters.length; i++)
            {
                this.parameters[i] = parameters[i].getName();
            }
        }


        // Implementations for Object.

        public boolean equals(Object o)
        {
            if (this == o) return true;
            if (o == null || getClass() != o.getClass()) return false;

            FlowTraceWriter.MethodSignature that = (FlowTraceWriter.MethodSignature)o;

            if (!name.equals(that.name)) return false;
            return Arrays.equals(parameters, that.parameters);
        }


        public int hashCode()
        {
            int result = name.hashCode();
            result = 31 * result + Arrays.hashCode(parameters);
            return result;
        }
    }
}
