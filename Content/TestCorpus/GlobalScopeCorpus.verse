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

CorpusFunctionEmpty() : void = {
}

CorpusFunctionIdentity(Input : int)<computes> : int =
    Input

CorpusFunctionAdd1(Input : int)<computes> : int =
    Input + 1

CorpusFunctionAddNegative1(Input : int)<computes> : int =
    Input + -1

CorpusFunctionSubtract1(Input : int)<computes> : int =
    Input - 1

CorpusFunctionMultiply1(Input : int)<computes> : int =
    Input * 1

CorpusFunctionDivide1(Input : int)<computes> : int =
    Input / 1

CorpusFunctionGreater1(Input : int)<computes> : bool =
    Input > 1

CorpusFunctionGreaterOrEqual1(Input : int)<computes> : bool =
    Input >= 1

CorpusConstantOne : string = "one"
CorpusConstantTwo : string = "two"

CorpusTypeAliasOne := type{_Value : int where 0 <= _Value, _Value <= 10}
CorpusTypeAliasTwo := type{_Value : int where 10 <= _Value, _Value <= 20}
