package proguard.inject;

import java.io.PrintWriter;
import java.io.StringWriter;
import java.util.Arrays;

public class FlowTraceWriter {

    public static final int JAVA_LOG_ENTER = 1;
    public static final int JAVA_LOG_EXIT = 2;
    public static final int JAVA_LOG_RUNNABLE = 4;
    static boolean initialized = false;
    public static native int initTraces();
    public static native void FlowTraceLogFlow(String thisClassName, String thisMethodName, String callClassName, String callMethodName, int log_type, int thisID, int callID, int thisLineNumber, int callLineNumber);
    public static native void FlowTraceLogTrace(int severity, String thisClassName, String thisMethodName, int callLineNumber, String tag, String msg, int isException);

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

        //System.out.println((((log_type & JAVA_LOG_ENTER) ==  JAVA_LOG_ENTER) ? " -> " : " <- ") + thisClassName + " " + thisMethodName + " "  + thisLineNumber + " " + callClassName + " " + callMethodName + " "  + callLineNumber);
        FlowTraceLogFlow(thisClassName, thisMethodName, callClassName, callMethodName, log_type, thisID, callID, thisLineNumber, callLineNumber);
    }

    static public void logTrace(int severity, String tag, String msg, Throwable exception)
    {
        String thisClassName = "";
        String thisMethodName = "";
        int callLineNumber = -1;
        int isException = 0;

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
            isException = 1;
            StringWriter writer = new StringWriter();
            PrintWriter printWriter= new PrintWriter(writer);
            exception.printStackTrace(printWriter);
            msg = msg + "\n" + writer.toString();
        }
        FlowTraceLogTrace(severity, thisClassName, thisMethodName, callLineNumber, tag, msg, isException);
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
