#pragma once

#include <algorithm>    // For std::max

#include "CodeGenHelpers.h"
#include "ExpressionTree.h"
#include "NativeJIT/X64CodeGenerator.h"
#include "Node.h"


namespace NativeJIT
{
    class ExpressionTree;

    template <JccType JCC>
    class FlagExpressionNode : public Node<bool>
    {
    public:
        FlagExpressionNode(ExpressionTree& tree);

        virtual void CodeGenFlags(ExpressionTree& tree) = 0;
    };


    template <typename T, JccType JCC>
    class ConditionalNode : public Node<T>
    {
    public:
        ConditionalNode(ExpressionTree& tree,
                        FlagExpressionNode<JCC>& condition,
                        Node<T>& trueExpression,
                        Node<T>& falseExpression);


        //
        // Overrides of Node methods.
        //
        virtual unsigned LabelSubtree(bool isLeftChild) override;
        virtual void Print() const override;


        //
        // Overrides of Node<T> methods.
        //
        virtual ExpressionTree::Storage<T> CodeGenValue(ExpressionTree& tree) override;


    private:
        FlagExpressionNode<JCC>& m_condition;
        Node<T>& m_trueExpression;
        Node<T>& m_falseExpression;
    };


    template <typename T, JccType JCC>
    class RelationalOperatorNode : public FlagExpressionNode<JCC>
    {
    public:
        RelationalOperatorNode(ExpressionTree& tree,
                               Node<T>& left,
                               Node<T>& right);


        //
        // Overrides of Node methods.
        //
        virtual unsigned LabelSubtree(bool isLeftChild) override;
        virtual void Print() const override;


        //
        // Overrides of Node<T> methods.
        //
        virtual ExpressionTree::Storage<bool> CodeGenValue(ExpressionTree& tree) override;


        //
        // Overrides of FlagExpression methods.
        //
        virtual void CodeGenFlags(ExpressionTree& tree) override;

    private:
        Node<T>& m_left;
        Node<T>& m_right;
    };


    //class LogicalAndNode : public FlagExpressionNode
    //{
    //public:
    //    LogicalAndNode(FlagExpressionNode& left, FlagExpressionNode& right);

    //private:
    //    FlagExpressionNode& m_left;
    //    FlagExpressionNode& m_right;
    //};


    //*************************************************************************
    //
    // Template definitions for FlagExpressionNode
    //
    //*************************************************************************
    template <JccType JCC>
    FlagExpressionNode<JCC>::FlagExpressionNode(ExpressionTree& tree)
        : Node(tree)
    {
    }


    //*************************************************************************
    //
    // Template definitions for ConditionalNode
    //
    //*************************************************************************
    template <typename T, JccType JCC>
    ConditionalNode<T, JCC>::ConditionalNode(ExpressionTree& tree,
                                             FlagExpressionNode<JCC>& condition,
                                             Node<T>& trueExpression,
                                             Node<T>& falseExpression)
        : Node(tree),
          m_condition(condition),
          m_trueExpression(trueExpression),
          m_falseExpression(falseExpression)
    {
    }


    template <typename T, JccType JCC>
    unsigned ConditionalNode<T, JCC>::LabelSubtree(bool /*isLeftChild*/)
    {
        unsigned condition = m_condition.LabelSubtree(true);
        unsigned trueExpression = m_trueExpression.LabelSubtree(true);
        unsigned falseExpression = m_falseExpression.LabelSubtree(true);

        // TODO: Might want to store the counts separately and only spill when necessary.

        SetRegisterCount((std::max)(condition, (std::max)(trueExpression, falseExpression)));

        return GetRegisterCount();
    }


    template <typename T, JccType JCC>
    void ConditionalNode<T, JCC>::Print() const
    {
        std::cout << "Conditional(" << X64CodeGenerator::JccName(JCC) << ") id=" << GetId();
        std::cout << ", parents = " << GetParentCount();
        std::cout << ", condition = " << m_condition.GetId();
        std::cout << ", trueExpression = " << m_trueExpression.GetId();
        std::cout << ", right = " << m_falseExpression.GetId();
        std::cout << ", ";
        PrintRegisterAndCacheInfo();
    }


    template <typename T, JccType JCC>
    typename ExpressionTree::Storage<T> ConditionalNode<T, JCC>::CodeGenValue(ExpressionTree& tree)
    {
        m_condition.CodeGenFlags(tree);

        // TODO: This will not work in cases where the false and true expressions
        // are more complex. The execution in NativeJIT has a continuous flow
        // regardless of the outcome of the condition whereas the generated x64 code
        // has two branches and each of them can have independent impact on
        // allocated and spilled registers. The code has to ensure that the state
        // of the allocated/spilled registers (i.e. all Storages) is consistent
        // once those two x64 branches converge back.
        // TODO: Pin the result register.
        X64CodeGenerator& code = tree.GetCodeGenerator();
        Label l1 = code.AllocateLabel();
        code.EmitConditionalJump<JCC>(l1);

        auto falseValue = m_falseExpression.CodeGen(tree);
        auto rFalse = falseValue.ConvertToDirect(true);

        Label l2 = code.AllocateLabel();
        code.Jmp(l2);

        code.PlaceLabel(l1);

        auto trueValue = m_trueExpression.CodeGen(tree);
        auto rTrue = trueValue.ConvertToDirect(false);

        if (!rTrue.IsSameHardwareRegister(rFalse))
        {
            // TODO: Do we always need to move the value?
            // In the case of caching, r2 may not be equal to r.
            // TODO: unit test for this case
            code.Emit<OpCode::Mov>(rFalse, rTrue);
        }

        code.PlaceLabel(l2);

        return falseValue;
    }


    //*************************************************************************
    //
    // Template definitions for RelationalOperator
    //
    //*************************************************************************
    template <typename T, JccType JCC>
    RelationalOperatorNode<T, JCC>::RelationalOperatorNode(ExpressionTree& tree,
                                                           Node<T>& left,
                                                           Node<T>& right)
        : FlagExpressionNode(tree),
          m_left(left),
          m_right(right)
    {
        m_left.IncrementParentCount();
        m_right.IncrementParentCount();
    }


    template <typename T, JccType JCC>
    unsigned RelationalOperatorNode<T, JCC>::LabelSubtree(bool /*isLeftChild*/)
    {
        unsigned left = m_left.LabelSubtree(true);
        unsigned right = m_right.LabelSubtree(false);

        SetRegisterCount(ComputeRegisterCount(left, right));

        // WARNING: GetRegisterCount() may return a different value than passed to SetRegisterCount().
        return GetRegisterCount();
    }


    template <typename T, JccType JCC>
    void RelationalOperatorNode<T, JCC>::Print() const
    {
        std::cout << "RelationalOperatorNode(" << X64CodeGenerator::JccName(JCC) << ") id=" << GetId();
        std::cout << ", parents = " << GetParentCount();
        std::cout << ", left = " << m_left.GetId();
        std::cout << ", right = " << m_right.GetId();
        std::cout << ", ";
        PrintRegisterAndCacheInfo();
    }


    template <typename T, JccType JCC>
    typename ExpressionTree::Storage<bool> RelationalOperatorNode<T, JCC>::CodeGenValue(ExpressionTree& tree)
    {
        X64CodeGenerator& code = tree.GetCodeGenerator();

        Label conditionIsTrue = code.AllocateLabel();
        Label testCompleted = code.AllocateLabel();

        // Evaluate the condition and react based on it.
        CodeGenFlags(tree);
        // Allocate the result register before the conditional jump so that
        // if any register gets spilled, the spill applies to both branches.
        // The spilling (i.e. the MOV instruction that is used to copy the
        // spilled value from the register onto stack) does not affect any flags.
        auto result = tree.Direct<bool>();
        code.EmitConditionalJump<JCC>(conditionIsTrue);

        code.EmitImmediate<OpCode::Mov>(result.GetDirectRegister(), false);
        code.Jmp(testCompleted);

        code.PlaceLabel(conditionIsTrue);
        code.EmitImmediate<OpCode::Mov>(result.GetDirectRegister(), true);

        code.PlaceLabel(testCompleted);

        return result;
    }


    template <typename T, JccType JCC>
    void RelationalOperatorNode<T, JCC>::CodeGenFlags(ExpressionTree& tree)
    {
        if (IsCached())
        {
            auto result = GetCache();
            ReleaseCache();

            // TODO: This code is wrong - need to set the correct JCC - not just the zero flag.
            // TODO: For this opcode to work, result must be direct. Might consider putting flags check into storage.
            auto direct = result.GetDirectRegister();
            tree.GetCodeGenerator().Emit<OpCode::Or>(direct, direct);
            throw 0;
        }
        else
        {
            unsigned l = m_left.GetRegisterCount();
            unsigned r = m_right.GetRegisterCount();

            Storage<T> sLeft;
            Storage<T> sRight;

            // Evaluate the side which uses more registers first.
            if (l >= r)
            {
                sLeft = m_left.CodeGen(tree);
                sRight = m_right.CodeGen(tree);
            }
            else
            {
                sRight = m_right.CodeGen(tree);
                sLeft = m_left.CodeGen(tree);
            }

            CodeGenHelpers::Emit<OpCode::Cmp>(tree.GetCodeGenerator(), sLeft.ConvertToDirect(false), sRight);
        }
    }
}
