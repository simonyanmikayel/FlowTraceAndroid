package proguard.injectOld;

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
import proguard.classfile.instruction.visitor.InstructionVisitor;
import proguard.classfile.util.ClassReferenceInitializer;
import proguard.classfile.util.ClassSubHierarchyInitializer;
import proguard.classfile.util.ClassUtil;
import proguard.classfile.util.SimplifiedVisitor;
import proguard.classfile.visitor.*;
import proguard.io.ClassPathDataEntry;
import proguard.io.ClassReader;
import proguard.util.MultiValueMap;
import proguard.classfile.attribute.annotation.*;
import proguard.classfile.attribute.annotation.target.*;
import java.io.IOException;
import static proguard.classfile.util.ClassUtil.internalClassName;

public class FlowTraceInjector
extends SimplifiedVisitor
implements
        ClassVisitor,
        MemberVisitor,
        AttributeVisitor,
        InstructionVisitor
{
    static final boolean DEBUG = false;
    private final Configuration configuration;
    private CodeAttributeEditor codeAttributeEditor;
    // Field acting as parameter for the visitor methods.
    private MultiValueMap<String, String> injectedClassMap;
    private ClassPool programClassPool;
    private ClassPool libraryClassPool;
    private InstructionSequenceBuilder ____;
    private final String LOGGER_CLASS_NAME = ClassUtil.internalClassName(proguard.inject.FlowTraceWriter.class.getName());

    private String thisClassName;
    private String thisMethodName;
    private int thisLineNumber;
    private int thisID;
    private int thisClassNameRef;
    private int thisMetodNameRef;
    private CallInstructions callInstructions[];
    private int callInstructionCount;

    public static final int LOG_INFO_ENTER = 0;
    public static final int LOG_INFO_EXIT = 1;
    public static final int LOG_INFO_TRACE = 2;
    public static final int LOG_EMPTY_METHOD_ENTER_EXIT = 3;
    public static final int LOG_INFO_ENTER_FIRST = 4;
    public static final int LOG_INFO_EXIT_LAST = 5;


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

        // Replace the instruction sequences in all non-ProGuard classes.
        String       regularExpression = "!proguard/**,!android/**,!java/**";
        //String       regularExpression = "!proguard/**";
        programClassPool.classesAccept(
                new ClassNameFilter(regularExpression,
                        this));
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

        ____ = new InstructionSequenceBuilder(programClass, programClassPool, libraryClassPool);
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
        callInstructions = new CallInstructions[codeAttribute.u4codeLength];
        callInstructionCount = 0;

        thisClassName = clazz.getName();
        thisMethodName = method.getName(clazz);
        thisLineNumber = codeAttribute.getLineNumber(0);
        thisID = thisClassName.hashCode() +  31 * thisMethodName.hashCode();
        thisClassNameRef = ____.getConstantPoolEditor().addStringConstant(thisClassName, clazz, null);
        thisMetodNameRef = ____.getConstantPoolEditor().addStringConstant(thisMethodName, clazz, null);

        codeAttribute.instructionsAccept(clazz, method, this);

        int lastInstruction = callInstructionCount - 1;
        for (int i = 0; i < callInstructionCount; i++)
        {
            try
            {
                ProgramClass programClass = (ProgramClass)clazz;
                RefConstant refConstant = (RefConstant)programClass.getConstant(callInstructions[i].constantInstruction.constantIndex);

                String callClassName = refConstant.getClassName(programClass);
                String callMethodName = refConstant.getName(programClass);
                int callLineNumber = codeAttribute.getLineNumber(callInstructions[i].offset);
                String callMethodType = refConstant.getType(programClass);
                int callID = callClassName.hashCode() +  31 * callMethodName.hashCode();

                int callClassNameRef = ____.getConstantPoolEditor().addStringConstant(callClassName, clazz, null);
                int callMetodNameRef = ____.getConstantPoolEditor().addStringConstant(callMethodName, clazz, null);

                codeAttributeEditor.insertBeforeInstruction(callInstructions[i].offset,
                        logInstruction(i == 0 ? LOG_INFO_ENTER_FIRST : LOG_INFO_ENTER, thisClassNameRef, thisMetodNameRef, callClassNameRef, callMetodNameRef, thisID, callID, thisLineNumber, callLineNumber));
                codeAttributeEditor.insertAfterInstruction(callInstructions[i].offset,
                        logInstruction(i == lastInstruction ? LOG_INFO_EXIT_LAST : LOG_INFO_EXIT, thisClassNameRef, thisMetodNameRef, callClassNameRef, callMetodNameRef, thisID, callID, thisLineNumber, callLineNumber));

                if (DEBUG)
                {
                    System.out.println("\t: " + thisClassName + " " + thisMethodName + " -> " + callClassName + " " + callMethodName + " " + callMethodType);
                }
            }
            catch (Exception e)
            {
                System.out.println("Exception on visitConstantInstruction: " + clazz.getName() + " " + method.getName(clazz) + " " + callInstructions[i].constantInstruction.getName() + " " + callInstructions[i].constantInstruction.constantIndex);
                System.out.println("Exception: " + e.toString());
            }
        }

        if (!codeAttributeEditor.isModified())
        {
            // no call within this method
            codeAttributeEditor.insertBeforeInstruction(0, logInstruction(LOG_EMPTY_METHOD_ENTER_EXIT, thisClassNameRef, thisMetodNameRef, thisClassNameRef, thisMetodNameRef, -1, thisID, -1, thisLineNumber));
        }

        //write if modified
        codeAttributeEditor.visitCodeAttribute(clazz, method, codeAttribute);
    }

    @Override
    public void visitConstantInstruction(Clazz clazz, Method method, CodeAttribute codeAttribute, int offset, ConstantInstruction constantInstruction)
    {
        if (DEBUG)
        {
//            System.out.println("visitConstantInstruction: " + clazz.getName() + " " + method.getName(clazz) + " " + constantInstruction.getName() + " " + constantInstruction.constantIndex);
        }

        if (constantInstruction.opcode == InstructionConstants.OP_INVOKEVIRTUAL ||
            constantInstruction.opcode == InstructionConstants.OP_INVOKESPECIAL ||
            constantInstruction.opcode == InstructionConstants.OP_INVOKESTATIC ||
            constantInstruction.opcode == InstructionConstants.OP_INVOKEINTERFACE ||
            constantInstruction.opcode == InstructionConstants.OP_INVOKEDYNAMIC)
        {
            callInstructions[callInstructionCount] = new CallInstructions(offset, constantInstruction);
            callInstructionCount++;
        }
    }

    private Instruction[] logInstruction(int log_type, int thisClassNameRef, int thisMetodNameRef, int callClassNameRef, int callMetodNameRef, int thisID, int callID, int thisLineNumber, int callLineNumber)
    {
        return   ____
                .ldc_(thisClassNameRef)
                .ldc_(thisMetodNameRef)
                .ldc_(callClassNameRef)
                .ldc_(callMetodNameRef)
                .ldc(log_type)
                .ldc(thisID)
                .ldc(callID)
                .ldc(thisLineNumber)
                .ldc(callLineNumber)
                .invokestatic(LOGGER_CLASS_NAME, "log", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;IIIII)V").__();
    }

    private class CallInstructions {
        int offset;
        ConstantInstruction constantInstruction;
        CallInstructions(int offset, ConstantInstruction constantInstruction) {
            this.offset = offset;
            this.constantInstruction = constantInstruction;
        }
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
