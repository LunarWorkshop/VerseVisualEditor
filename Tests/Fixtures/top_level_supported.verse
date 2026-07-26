# Every supported file-scope definition kind, deliberately separated by raw trivia.
(/Example.org:)ExampleModule := module {}

ExampleClass<public> := class<concrete> {}

ExampleStruct := struct {}

ExampleInterface := interface {}

ExampleEnum := enum { One, Two }

ExampleFunction(Input : int)<computes> : int =
    Input

var MutableValue : int = 1

ConstantValue : string = "unchanged"

IntegerAlias := type{_X : int where 0 <= _X, _X <= 10}

using { /Verse.org/Simulation }
