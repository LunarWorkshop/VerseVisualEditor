# Global-scope visual tile corpus: first comment.
# A continuation of the first comment.
<# Global-scope visual tile corpus: second comment. #>

CorpusModuleOne := module {}
CorpusModuleTwo := module:
    CorpusModuleTwoPointOne := module {}
    CorpusModuleTwoPointTwo := module {
        CorpusClass221 := class {
            var Counter : int = 0
            var IncrementAmount : int = 1
            Increment(Value : int)<computes> : int = Value + 1
            Double(Value : int)<computes> : int = Value * 2
        }
        CorpusClass222 := class:
            var Score : int = 100
            var Penalty : int = 5
            Decrement(Value : int)<computes> : int = Value - 1
            Triple(Value : int)<computes> : int = Value * 3
    }

CorpusClassOne := class {}
CorpusClassTwo := class {}

CorpusStructOne := struct {}
CorpusStructTwo := struct {}

CorpusInterfaceOne := interface {}
CorpusInterfaceTwo := interface {}

CorpusEnumOne := enum { First, Second }
CorpusEnumTwo := enum { Alpha, Beta }

AssortedFunctions := module:
    MapRangeUnclamped(Value: float, InLow: float, InHigh: float, OutLow: float, OutHigh: float) : float =
        if:
            Denom: float = InHigh - InLow
            Denom <> 0.0
        then:
            OutLow + (OutHigh - OutLow) * ((Value - InLow) / Denom)
        else:
            OutLow

Basics := module:
    CorpusFunctionEmpty() : void = {
    }

    CorpusFunctionIdentity(Input : int)<computes> : int =
        Input

    CorpusFunctionCast(Input : float)<computes><decides><reads> : int =
        Int[Input]

    CorpusFunctionCall(Input : int)<computes> : int =
        Ceil(Input)

    CorpusFunctionLiteralFloat()<computes> : float =
        1.0

    CorpusFunctionLiteralInt()<computes> : int =
        42

BinaryOperators := module:
    CorpusFunctionAdd1(Input : int)<computes> : int =
        Input + 1

    CorpusFunctionAddNegative1(Input : int)<computes> : int =
        Input + -1

    CorpusFunctionSubtract1(Input : int)<computes> : int =
        Input - 1

    CorpusFunctionMultiply1(Input : int)<computes> : int =
        Input * 1

    CorpusFunctionDivide1(Input : int)<decides> : rational =
        Input / 1

    CorpusFunctionGreater1(Input : int)<decides> : void =
        Input > 1

    CorpusFunctionGreaterOrEqual1(Input : int)<decides> : void =
        Input >= 1

    Complex(A: float, B: float, C: float, D: float, E: float, F: float)<decides> : float =
        A + (B - C) * ((D - E) / F)

ConstantsAndVariables := module:
    CorpusFunctionConstant(Input : int)<computes> : int =
        Constant: int = 1
        Input + Constant

    CorpusFunctionConstantAssigned(Input : int)<computes> : int =
        Constant: int = Input
        Input + Constant

    CorpusFunctionVariable(Input : int)<allocates><reads> : int =
        var Variable: int = 1
        Input + Variable

    CorpusFunctionVariableAssigned(Input : int)<allocates><reads><writes> : int =
        var Variable: int = Input
        set Variable = Input + 1
        Input + Variable

Control := module:
    CorpusFunctionIf(Input : int)<computes> : float =
        if (Input >= 1):
            return 42.0
        69.0

    CorpusFunctionIfWithDefinitions(Input : int)<computes> : float =
        if:
            Variable: float = 42.0
            Input >= 1
        then:
            return Variable
        69.0

    CorpusFunctionFor(Input : int)<computes><reads><allocates><writes> : int =
        var Result: int = 0
        Values:[]float = array{1.0, 2.0, 3.0}
        for (Value : Values):
            set Result += 1
        Result

    CorpusFunctionLoop(Input : int)<computes><reads><writes><allocates> : float =
        var Variable:int = 0
        loop:
            set Variable += 1
            if (Variable >= Input):
                break
        69.0

CorpusConstantOne : string = "one"
CorpusConstantTwo : string = "two"

CorpusTypeAliasOne := type{_Value : int where 0 <= _Value, _Value <= 10}
CorpusTypeAliasTwo := type{_Value : int where 10 <= _Value, _Value <= 20}
