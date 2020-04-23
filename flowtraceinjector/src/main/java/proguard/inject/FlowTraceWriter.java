package proguard.inject;

import java.util.Arrays;

public class FlowTraceWriter {

    private static final boolean DEBUG = false;
    static boolean initialized = false;
    public static native int initTraces();
    public static native void FlowTraceLogFlow(int log_type, int log_flags, String thisClassName, String thisMethodName, int thisID, int callID, int thisLineNumber, int callLineNumber);
    public static native void FlowTraceLogTrace(int priority, String thisClassName, String thisMethodName, int thisLineNumber, int callLineNumber, String tag, String msg, int flags);

    public static final int LOG_INFO_ENTER = 0;
    public static final int LOG_INFO_EXIT = 1;
    public static final int LOG_FLAG_JAVA = 2;
    public static final int LOG_FLAG_EXCEPTION = 4;
    public static final int LOG_FLAG_RUNNABLE_INIT = 8;
    public static final int LOG_FLAG_RUNNABLE_RUN = 16;
    public static final int LOG_FLAG_OUTER_LOG = 32;

    private static int s_log_type;
    private static int s_log_flags;
    private static int s_thisID;
    private static String s_thisClassName;
    private static String s_thisMethodName;
    private static int s_thisLineNumber;
    private static long s_tid;

    static {
        if (!DEBUG) {
            System.loadLibrary("flowtrace");
            initialized = (0 != initTraces());
        }
    }


    static public synchronized void logFlow(int thisID, int log_type, int log_flags, String thisClassName, String thisMethodName, int thisLineNumber) {
        if (DEBUG)
            System.out.println( ((log_type == 0) ? "FlowTrace => " : "FlowTrace <= ") + " class: " + thisClassName + " method: " + thisMethodName + " line: " + thisLineNumber + " flags: " + log_flags);

        if (!initialized)
            return;

        long tid = Thread.currentThread().getId();
        int callLineNumber = 0;

        boolean isEnter = (log_type == LOG_INFO_ENTER);
        boolean isOuterLog = ((log_flags & LOG_FLAG_OUTER_LOG) == LOG_FLAG_OUTER_LOG);

//        if (isEnter) {
//            final StackTraceElement[] stacktrace = Thread.currentThread().getStackTrace();
//            if (stacktrace != null && stacktrace.length > 2)
//            {
//                callClassName = stacktrace[2].getClassName();
//                callMethodName =  stacktrace[2].getClassName();
//                callLineNumber = stacktrace[2].getLineNumber();
//            }
//        }

        if (isOuterLog) {
            if (s_thisID != 0) {
                FlowTraceLogFlow(s_log_type, s_log_flags, s_thisClassName, s_thisMethodName, s_thisID, 0, s_thisLineNumber, 0);
            }
            s_log_type = log_type;
            s_log_flags = log_flags;
            s_thisID = thisID;
            s_thisClassName = thisClassName;
            s_thisMethodName = thisMethodName;
            s_thisLineNumber = thisLineNumber;
            s_tid = tid;
        } else {

            if (s_tid == tid && s_thisID == thisID && isEnter) {
                callLineNumber = s_thisLineNumber;
            } else if (s_thisID != 0) {
                FlowTraceLogFlow(s_log_type, s_log_flags, s_thisClassName, s_thisMethodName, s_thisID, 0, s_thisLineNumber, 0);
            }

            FlowTraceLogFlow(log_type, log_flags, thisClassName, thisMethodName, thisID, 0, thisLineNumber, callLineNumber);

            s_tid = 0;
            s_thisID = 0;
        }
    }

    static public synchronized void logRunnable(int runnableMethod, Object o)
    {
        if (DEBUG)
            System.out.println("   ----------------->" + (runnableMethod == 1 ? " <init> " : " run ") + o.hashCode());

        if (!initialized)
            return;

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

        //first parameter is not used in Java_proguard_inject_FlowTraceWriter_FlowTraceLogTrace
        FlowTraceLogTrace(0, thisClassName, thisMethodName, o.hashCode(), callLineNumber, "Runnable id " + o.hashCode(), runnableMethod == 1 ? "<init>" : "run", flags);
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
