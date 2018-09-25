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
    static final boolean DEBUG = true;
    private final Configuration configuration;
    private CodeAttributeEditor codeAttributeEditor;
    // Field acting as parameter for the visitor methods.
    private MultiValueMap<String, String> injectedClassMap;
    private ClassPool programClassPool;
    private ClassPool libraryClassPool;
    private InstructionSequenceBuilder ____;
    private final String LOGGER_CLASS_NAME = ClassUtil.internalClassName(proguard.inject.FlowTraceWriter.class.getName());

    private int returnOffset;
    private boolean inRunnable = false;


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

    public boolean isRunnable(ProgramClass programClass)
    {
        if (programClass.getInterfaceCount() < 1)
            return false;

        String interfaceName = programClass.getInterfaceName(0);
        if (interfaceName == null)
            return false;

        return interfaceName.equals("java/lang/Runnable");
    }

    int enterParam() {
        return inRunnable ? (FlowTraceWriter.JAVA_LOG_ENTER | FlowTraceWriter.JAVA_LOG_RUNNABLE) : FlowTraceWriter.JAVA_LOG_ENTER;
    }

    int exitParam() {
        return inRunnable ? (FlowTraceWriter.JAVA_LOG_EXIT | FlowTraceWriter.JAVA_LOG_RUNNABLE) : FlowTraceWriter.JAVA_LOG_EXIT;
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

        inRunnable = isRunnable(programClass);

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
        returnOffset = 0;

        codeAttribute.instructionsAccept(clazz, method, this);

        if (returnOffset == 0)
        {
            codeAttributeEditor.insertBeforeInstruction(0,
                    ____.
                            ldc(enterParam()).
                            invokestatic(LOGGER_CLASS_NAME, "logFlow", "(I)V").
                            ldc(exitParam()).
                            invokestatic(LOGGER_CLASS_NAME, "logFlow", "(I)V").
                            __());
        }
        else
        {
            codeAttributeEditor.insertBeforeInstruction(0,
                    ____.
                            ldc(enterParam()).
                            invokestatic(LOGGER_CLASS_NAME, "logFlow", "(I)V").
                            __());
        }

        //write if modified
        codeAttributeEditor.visitCodeAttribute(clazz, method, codeAttribute);
    }

    @Override
    //public void visitBranchInstruction(Clazz clazz, Method method, CodeAttribute codeAttribute, int offset, BranchInstruction instruction)
    public void visitSimpleInstruction(Clazz clazz, Method method, CodeAttribute codeAttribute, int offset, SimpleInstruction instruction)
    //public void visitConstantInstruction(Clazz clazz, Method method, CodeAttribute codeAttribute, int offset, ConstantInstruction instruction)
    {
        if (DEBUG)
        {
//            System.out.println("visitConstantInstruction: " + clazz.getName() + " " + method.getName(clazz) + " " + constantInstruction.getName() + " " + constantInstruction.constantIndex);
        }

        if (instruction.opcode == InstructionConstants.OP_IRETURN ||
                instruction.opcode == InstructionConstants.OP_LRETURN ||
                instruction.opcode == InstructionConstants.OP_FRETURN ||
                instruction.opcode == InstructionConstants.OP_DRETURN ||
                instruction.opcode == InstructionConstants.OP_ARETURN ||
                instruction.opcode == InstructionConstants.OP_RETURN)
        {
            returnOffset = offset;
            if (offset != 0)
            {
                codeAttributeEditor.insertBeforeInstruction(offset,
                        ____.
                                ldc(exitParam()).
                                invokestatic(LOGGER_CLASS_NAME, "logFlow", "(I)V").
                                __());
            }
            if (DEBUG)
            {
                System.out.println("visit: " + clazz.getName() + " " + method.getName(clazz) + " " + instruction.getName());
            }
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
