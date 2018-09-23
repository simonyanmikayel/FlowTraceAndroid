package proguard.injectOld;

import java.util.Arrays;

public class FlowTraceWriter {

    static boolean initialized = false;
    public static native int initTraces();
    public static native void FlowTraceSendFlow(String thisClassName, String thisMethodName, String callClassName, String callMethodName, int log_type, int thisID, int callID, int thisLineNumber, int callLineNumber);

    static {
        System.loadLibrary("flowtrace");
        initialized = (0 != initTraces());
    }

    static public void log(String thisClassName, String thisMethodName, String callClassName, String callMethodName, int log_type, int thisID, int callID, int thisLineNumber, int callLineNumber) {
        //System.out.println((log_type == 0 ? "Before -> " : "After <- ") + thisClassName + " " + thisMethodName + " "  + thisLineNumber + " " + callClassName + " " + callMethodName + " "  + callLineNumber);
        FlowTraceSendFlow(thisClassName, thisMethodName, callClassName, callMethodName, log_type, thisID, callID, thisLineNumber, callLineNumber);
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
