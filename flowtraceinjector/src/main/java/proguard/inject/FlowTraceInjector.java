package proguard.inject;

import proguard.Configuration;
import proguard.classfile.*;
import proguard.classfile.attribute.Attribute;
import proguard.classfile.attribute.CodeAttribute;
import proguard.classfile.attribute.preverification.StackMapFrame;
import proguard.classfile.attribute.preverification.VerificationType;
import proguard.classfile.attribute.visitor.AttributeVisitor;
import proguard.classfile.constant.*;
import proguard.classfile.editor.CodeAttributeEditor;
import proguard.classfile.editor.InstructionSequenceBuilder;
import proguard.classfile.instruction.ConstantInstruction;
import proguard.classfile.instruction.Instruction;
import proguard.classfile.instruction.InstructionConstants;
import proguard.classfile.instruction.SimpleInstruction;
import proguard.classfile.instruction.visitor.InstructionVisitor;
import proguard.classfile.util.ClassReferenceInitializer;
import proguard.classfile.util.ClassSubHierarchyInitializer;
import proguard.classfile.util.ClassUtil;
import proguard.classfile.util.SimplifiedVisitor;
import proguard.classfile.visitor.*;
import proguard.io.ClassPathDataEntry;
import proguard.io.ClassReader;
import proguard.util.ClassNameParser;
import proguard.util.ListParser;
import proguard.util.MultiValueMap;
import proguard.classfile.attribute.annotation.*;
import proguard.classfile.attribute.annotation.target.*;
import proguard.util.StringMatcher;

import java.io.IOException;
import static proguard.classfile.util.ClassUtil.internalClassName;
import static proguard.inject.FlowTraceWriter.LOG_FLAG_OUTER_LOG;
import static proguard.inject.FlowTraceWriter.LOG_INFO_ENTER;
import static proguard.inject.FlowTraceWriter.LOG_INFO_EXIT;

public class FlowTraceInjector
        extends SimplifiedVisitor
        implements
        ClassVisitor,
        MemberVisitor,
        AttributeVisitor,
        InstructionVisitor
{
    private static final boolean DEBUG = false;
    private static final boolean verbose = false; //TODO set as config value
    private static final boolean injectRunnable = true;

    private final Configuration configuration;
    private CodeAttributeEditor codeAttributeEditor;
    // Field acting as parameter for the visitor methods.
    private MultiValueMap<String, String> injectedClassMap;
    private ClassPool programClassPool;
    private ClassPool libraryClassPool;
    private InstructionSequenceBuilder ____;
    private final String LOGGER_CLASS_NAME = ClassUtil.internalClassName(proguard.inject.FlowTraceWriter.class.getName());

    private int returnOffset;
    private int runnableID = 1;
    private boolean inRunnable;
    private int unknownRef;
    private int invoceInstruction = 0;
    private StringMatcher regularExpressionMatcher;


    /**
     * Creates a new TraceInjector.
     */
    public FlowTraceInjector(Configuration configuration)
    {
        this.configuration = configuration;
        codeAttributeEditor = new CodeAttributeEditor(true, true);
    }

    /**
     * Instrumets the given program class pool.
     */
    public void execute(ClassPool programClassPool,
                        ClassPool                     libraryClassPool,
                        MultiValueMap<String, String> injectedClassMap )
    {
        this.programClassPool = programClassPool;
        this.libraryClassPool = libraryClassPool;

        ClassReader classReader =
                new ClassReader(false, false, false, null,
                        new MultiClassVisitor(
                                new ClassPoolFiller(programClassPool),
                                new ClassReferenceInitializer(programClassPool, libraryClassPool),
                                new ClassSubHierarchyInitializer()
                        ));

        try
        {
            classReader.read(new ClassPathDataEntry(FlowTraceWriter.MethodSignature.class));
            classReader.read(new ClassPathDataEntry(FlowTraceWriter.class));
        }
        catch (IOException e)
        {
            throw new RuntimeException(e);
        }

        // Set the injected class map for the extra visitor.
        this.injectedClassMap = injectedClassMap;

        StringBuilder sb = new StringBuilder();
        sb.append("!proguard/**");
        if (configuration.flowTracesFilter != null) {
            for (Object o: configuration.flowTracesFilter) {
                sb.append(",");
                sb.append(o.toString());
            }
        }
        String regularExpression = sb.toString();

        regularExpressionMatcher = new ListParser(new ClassNameParser(null)).parse(regularExpression);

        //regularExpression = "!proguard/**,!android/**,!java/**";
        programClassPool.classesAccept(
                new ClassNameFilter(regularExpression,
                        this));
    }

    public void checkRunnable(ProgramClass programClass)
    {
        if (programClass.getInterfaceCount() < 1)
            return;

        String interfaceName = programClass.getInterfaceName(0);
        if (interfaceName == null)
            return;

        if (!interfaceName.equals("java/lang/Runnable"))
            return;

        inRunnable = true;
        ++runnableID;
    }

    @Override
    public void visitProgramClass(ProgramClass programClass)
    {
        if (DEBUG)
        {
            System.out.println("visitProgramClass: " + programClass.getName());
        }
        injectedClassMap.put(programClass.getName(), internalClassName(FlowTraceWriter.class.getName()));
        injectedClassMap.put(programClass.getName(), internalClassName(FlowTraceWriter.MethodSignature.class.getName()));

        inRunnable = false;
        invoceInstruction = 0;

        if (injectRunnable)
            checkRunnable(programClass);

        ____ = new InstructionSequenceBuilder(programClass, programClassPool, libraryClassPool);
        unknownRef = ____.getConstantPoolEditor().addStringConstant("?", programClass, null);

        programClass.methodsAccept(this);
    }

    @Override
    public void visitProgramMethod(ProgramClass programClass, ProgramMethod programMethod)
    {
        programMethod.attributesAccept(programClass, this);
    }

    @Override
    public void visitCodeAttribute(Clazz clazz, Method method, CodeAttribute codeAttribute)
    {
        // Set up the code attribute editor.
        codeAttributeEditor.reset(codeAttribute.u4codeLength);
        returnOffset = 0;

        codeAttribute.instructionsAccept(clazz, method, this);

        if (returnOffset != 0)
        {
            try
            {
                int runnableMethod = 0;
                if (inRunnable && injectRunnable) {
                    if (invoceInstruction == InstructionConstants.OP_INVOKEVIRTUAL && method.getName(clazz).equals("run"))
                        runnableMethod = 2;
                }
                String thisClassName = clazz.getName();
                String thisMethodName = method.getName(clazz);
                int thisLineNumber = codeAttribute.getLineNumber(0);

                if (regularExpressionMatcher.matches(thisClassName) )
                {
                    int thisClassNameRef = ____.getConstantPoolEditor().addStringConstant(thisClassName, clazz, null);
                    int thisMetodNameRef = ____.getConstantPoolEditor().addStringConstant(thisMethodName, clazz, null);
                    int thisID = thisClassName.hashCode() +  31 * thisMethodName.hashCode();
                    codeAttributeEditor.insertBeforeInstruction(0, logInstruction(thisID, runnableMethod, LOG_INFO_ENTER, 0, thisClassNameRef, thisMetodNameRef, thisLineNumber));
//                    if (thisClassName.contains("TMCServiceContract"))
//                        System.out.println("~~~LOG_INFO_ENTER : thisClassName: " + thisClassName + " thisMethodName: " + thisMethodName + " thisLineNumber: " + thisLineNumber + " Descriptor: " + method.getDescriptor(clazz));
                }
            }
            catch (Exception e)
            {
                System.out.println("Exception on injection: clazzName: " + clazz.getName() + " methodName: " + method.getName(clazz) + " Descriptor: " + method.getDescriptor(clazz));
                System.out.println("Exception: " + e.toString());
                throw(e);
            }
        }

        //write if modified
        codeAttributeEditor.visitCodeAttribute(clazz, method, codeAttribute);
    }


    public void visitSimpleInstruction(Clazz clazz, Method method, CodeAttribute codeAttribute, int offset, SimpleInstruction instruction)
    {
        if (
                //instruction.opcode == InstructionConstants.OP_RET || - this is no a return but continue execution from address taken from a local variable #index
                instruction.opcode == InstructionConstants.OP_IRETURN ||
                instruction.opcode == InstructionConstants.OP_LRETURN ||
                instruction.opcode == InstructionConstants.OP_FRETURN ||
                instruction.opcode == InstructionConstants.OP_DRETURN ||
                instruction.opcode == InstructionConstants.OP_ARETURN ||
                instruction.opcode == InstructionConstants.OP_RETURN)
        {
            returnOffset = offset;
            if (offset != 0)
            {
                try
                {
                    int runnableMethod = 0;
                    if (inRunnable && injectRunnable) {
                        if (invoceInstruction == InstructionConstants.OP_INVOKESPECIAL && method.getName(clazz).equals("<init>"))
                            runnableMethod = 1;
                    }

                    String thisClassName = clazz.getName();
                    String thisMethodName = method.getName(clazz);
                    int thisLineNumber = codeAttribute.getLineNumber(0);

                    if (regularExpressionMatcher.matches(thisClassName))
                    {
                        int thisClassNameRef = ____.getConstantPoolEditor().addStringConstant(thisClassName, clazz, null);
                        int thisMetodNameRef = ____.getConstantPoolEditor().addStringConstant(thisMethodName, clazz, null);
                        int thisID = thisClassName.hashCode() +  31 * thisMethodName.hashCode();
                        codeAttributeEditor.insertBeforeInstruction(offset, logInstruction(thisID, runnableMethod, LOG_INFO_EXIT, 0, thisClassNameRef, thisMetodNameRef, thisLineNumber));
//                        if (thisClassName.contains("TMCServiceContract"))
//                            System.out.println("~~~LOG_INFO_EXIT : thisClassName: " + thisClassName + " thisMethodName: " + thisMethodName + " thisLineNumber: " + thisLineNumber + " Descriptor: " + method.getDescriptor(clazz));
                    }
                }
                catch (Exception e)
                {
                    System.out.println("Exception on injection: " + clazz.getName() + " " + method.getName(clazz) + " " + instruction.getName());
                    System.out.println("Exception:  " + e.getMessage());
                    throw(e);
                }

            }
            if (DEBUG)
            {
                System.out.println("visitSimpleInstruction: " + clazz.getName() + " " + method.getName(clazz) + " " + instruction.getName());
            }
        }
    }

    public void visitConstantInstruction(Clazz clazz, Method method, CodeAttribute codeAttribute, int offset, ConstantInstruction constantInstruction)
    {
        if (constantInstruction.opcode == InstructionConstants.OP_INVOKEVIRTUAL ||
                constantInstruction.opcode == InstructionConstants.OP_INVOKESPECIAL ||
                constantInstruction.opcode == InstructionConstants.OP_INVOKESTATIC ||
                constantInstruction.opcode == InstructionConstants.OP_INVOKEINTERFACE
                // || constantInstruction.opcode == InstructionConstants.OP_INVOKEDYNAMIC = TODO
        )
        {

            invoceInstruction = constantInstruction.opcode;

            try
            {
//                String callerClassName = clazz.getName();
//                String callerMethodName = method.getName(clazz);
                ProgramClass programClass = (ProgramClass)clazz;
                RefConstant refConstant = (RefConstant)programClass.getConstant(constantInstruction.constantIndex);
                String calledClassName = refConstant.getClassName(programClass);
                String calledMethodName = refConstant.getName(programClass);
//                int callerLineNumber = 0;//codeAttribute.getLineNumber(0);
                int calledLineNumber = codeAttribute.getLineNumber(offset);

                if (regularExpressionMatcher.matches(calledClassName))
                //if (verbose || !(calledClassName.startsWith("android/") || calledClassName.startsWith("java/")))
                {
                    int calledClassNameRef = ____.getConstantPoolEditor().addStringConstant(calledClassName, clazz, null);
                    int calledMetodNameRef = ____.getConstantPoolEditor().addStringConstant(calledMethodName, clazz, null);
//                    int callClassNameRef = ____.getConstantPoolEditor().addStringConstant(callerClassName, clazz, null);
//                    int callMetodNameRef = ____.getConstantPoolEditor().addStringConstant(callerMethodName, clazz, null);

//                    if (callerClassName.contains("TMCServiceContract"))
//                        System.out.println("~~~LOG_INFO_URAP : callerClassName: " + callerClassName + " callerMethodName: " + callerMethodName + " callerLineNumber: " + callerLineNumber + " Descriptor: " + method.getDescriptor(clazz) + " calledClassName: " + calledClassName + " calledMethodName: " + calledMethodName);
                    int thisID = calledClassName.hashCode() +  31 * calledMethodName.hashCode();
                    codeAttributeEditor.insertBeforeInstruction(offset, logInstruction(thisID, 0, LOG_INFO_ENTER, LOG_FLAG_OUTER_LOG, calledClassNameRef, calledMetodNameRef, calledLineNumber));
//                    codeAttributeEditor.insertAfterInstruction(offset, logInstruction(0, LOG_INFO_EXIT, LOG_FLAG_OUTER_LOG, thisClassNameRef, thisMetodNameRef, callClassNameRef, callMetodNameRef, callerLineNumber, calledLineNumber));
                }
                else
                {
                    if (DEBUG)
                    {
                        System.out.println("Skipping: " + calledClassName);
                    }
                }
            }
            catch (Exception e)
            {
                System.out.println("Exception on injection: " + clazz.getName() + " " + method.getName(clazz) + " " + constantInstruction.getName() + " constantIndex:" + constantInstruction.constantIndex + " opcode: " + constantInstruction.opcode);
                System.out.println("Exception: " + e.toString());
                //e.printStackTrace();
                throw(e);
            }
        }

    }

    private Instruction[] logInstruction(int thisID, int runnableMethod, int log_type, int log_flags, int thisClassNameRef, int thisMetodNameRef, int callLineNumber) {
        ____
                .ldc(thisID)
                .ldc(log_type)
                .ldc(log_flags)
                .ldc_(thisClassNameRef)
                .ldc_(thisMetodNameRef)
                .ldc(callLineNumber)
                .invokestatic(LOGGER_CLASS_NAME, "logFlow", "(IIILjava/lang/String;Ljava/lang/String;I)V");

        if (runnableMethod != 0) {
            ____.
                    ldc(runnableMethod).
                    aload_0().
                    invokestatic(LOGGER_CLASS_NAME, "logRunnable", "(ILjava/lang/Object;)V");
        }
        return ____.instructions();
    }

    public void visitAnyClass(Clazz clazz){}
    public void visitAnyMember(Clazz clazz, Member member){}
    public void visitAnyConstant(Clazz clazz, Constant constant){}
    public void visitAnyPrimitiveArrayConstant(Clazz clazz, PrimitiveArrayConstant primitiveArrayConstant, Object values){}
    public void visitAnyPrimitiveArrayConstantElement(Clazz clazz, PrimitiveArrayConstant primitiveArrayConstant, int index){}
    public void visitAnyAttribute(Clazz clazz, Attribute attribute){}
    public void visitAnyInstruction(Clazz clazz, Method method, CodeAttribute codeAttribute, int offset, Instruction instruction){}
    public void visitAnyStackMapFrame(Clazz clazz, Method method, CodeAttribute codeAttribute, int offset, StackMapFrame stackMapFrame){}
    public void visitAnyVerificationType(Clazz clazz, Method method, CodeAttribute codeAttribute, int offset, VerificationType verificationType){}
    public void visitAnnotation(Clazz clazz, Annotation annotation){}
    public void visitTypeAnnotation(Clazz clazz, TypeAnnotation typeAnnotation){}
    public void visitAnyTargetInfo(Clazz clazz, TypeAnnotation typeAnnotation, TargetInfo targetInfo){}
    public void visitTypePathInfo(Clazz clazz, TypeAnnotation typeAnnotation, TypePathInfo typePathInfo){}
    public void visitAnyElementValue(Clazz clazz, Annotation annotation, ElementValue elementValue){}
}
