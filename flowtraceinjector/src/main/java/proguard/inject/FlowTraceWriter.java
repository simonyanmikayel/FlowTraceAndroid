package proguard.inject;

import java.io.PrintWriter;
import java.io.StringWriter;
import java.util.Arrays;

public class FlowTraceWriter {

    private static final boolean DEBUG = false;
    static boolean initialized = false;
    public static native int initTraces();
    public static native void FlowTraceLogFlow(int log_type, int log_flags, String thisClassName, String thisMethodName, String callClassName, String callMethodName, int thisID, int callID, int thisLineNumber, int callLineNumber);
    public static native void FlowTraceLogTrace(int severity, String thisClassName, String thisMethodName, int thisLineNumber, int callLineNumber, String tag, String msg, int flags);

    public static final int LOG_INFO_ENTER = 0;
    public static final int LOG_INFO_EXIT = 1;
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
    public static final int LOG_FLAG_INNER_LOG= 32;

    private static int s_log_type;
    private static int s_log_flags;
    private static String s_thisClassName;
    private static String s_thisMethodName;
    private static String s_callClassName;
    private static String s_callMethodName;
    private static int s_thisLineNumber;
    private static int s_callLineNumber;
    private static int s_thisID;
    private static int s_callID;
    private static long s_tid;
    private static boolean s_flowSaved;

    static {
        if (!DEBUG) {
            System.loadLibrary("flowtrace");
            initialized = (0 != initTraces());
            //System.out.println("flowtrace initialized = " + initialized);
        }
    }

    static public synchronized void logFlow(int log_type, int log_flags, String thisClassName, String thisMethodName, String callClassName, String callMethodName, int thisLineNumber, int callLineNumber) {

        if (DEBUG)
            System.out.println( (log_type == 0) ? " -> " : " <- " + thisClassName + " " + thisMethodName + " "  + thisLineNumber + " <> " + callClassName + " " + callMethodName + " "  + callLineNumber);

        if (!initialized)
            return;

        long tid = Thread.currentThread().getId();
        int thisID = thisClassName.hashCode() +  31 * thisMethodName.hashCode();
        int callID = callClassName.hashCode() +  31 * callMethodName.hashCode();

        boolean isEnter = (log_type == LOG_INFO_ENTER);
        boolean isInnerLog = ((log_flags & LOG_FLAG_INNER_LOG )== LOG_FLAG_INNER_LOG);
        boolean sendSaved = false;
        boolean sendCurrent = true;

        if (s_flowSaved)
        {
            if (s_tid == tid && s_thisID == thisID)
            {
                boolean s_isInnerLog = ((s_log_flags & LOG_FLAG_INNER_LOG )== LOG_FLAG_INNER_LOG);
                boolean s_isEnter = (s_log_type == LOG_INFO_ENTER);
                if ((!isInnerLog && isEnter) && (s_isInnerLog && s_isEnter))
                {
                    s_thisLineNumber = thisLineNumber;
                    sendSaved = true;
                    sendCurrent = false;
                }
                if ((isInnerLog && !isEnter) && (!s_isInnerLog && !s_isEnter))
                {
                    sendCurrent = false;
                }
            } else {
                sendSaved = true;
            }
            s_flowSaved = false;
        }

        if (sendSaved)
            FlowTraceLogFlow(s_log_type, s_log_flags, s_thisClassName, s_thisMethodName, s_callClassName, s_callMethodName, s_thisID, s_callID, s_thisLineNumber, s_callLineNumber);

        if (sendCurrent)
            FlowTraceLogFlow(log_type, log_flags, thisClassName, thisMethodName, callClassName, callMethodName, thisID, callID, thisLineNumber, callLineNumber);

        //save inner ENTER and
        if ( (isInnerLog && isEnter) || (!isInnerLog && !isEnter) )
        {
            s_log_type       = log_type;
            s_log_flags      = log_flags;
            s_thisClassName  = thisClassName;
            s_thisMethodName = thisMethodName;
            s_callClassName  = callClassName;
            s_callMethodName = callMethodName;
            s_thisLineNumber = thisLineNumber;
            s_callLineNumber = callLineNumber;
            s_thisID         = thisID;
            s_callID         = callID;
            s_tid            = tid;

            s_flowSaved      = true;
        }
        else {
            s_flowSaved = false;
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

        FlowTraceLogTrace(LOG_DEBUG, thisClassName, thisMethodName, o.hashCode(), callLineNumber, "Runnable id " + o.hashCode(), runnableMethod == 1 ? "<init>" : "run", flags);
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
