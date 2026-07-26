# Global-scope visual block corpus: first comment.
<# Global-scope visual block corpus: second comment. #>

CorpusModuleOne := module {}
CorpusModuleTwo := module {}

CorpusClassOne := class {}
CorpusClassTwo := class {}

CorpusStructOne := struct {}
CorpusStructTwo := struct {}

CorpusInterfaceOne := interface {}
CorpusInterfaceTwo := interface {}

CorpusEnumOne := enum { First, Second }
CorpusEnumTwo := enum { Alpha, Beta }

CorpusFunctionOne(Input : int)<computes> : int =
    Input

CorpusFunctionTwo(Input : int)<computes> : int =
    Input + 1

var CorpusVariableOne : int = 1
var CorpusVariableTwo : int = 2

CorpusConstantOne : string = "one"
CorpusConstantTwo : string = "two"

CorpusTypeAliasOne := type{_Value : int where 0 <= _Value, _Value <= 10}
CorpusTypeAliasTwo := type{_Value : int where 10 <= _Value, _Value <= 20}
