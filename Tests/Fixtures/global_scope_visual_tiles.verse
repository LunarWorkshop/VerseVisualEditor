# Stable global-scope visual tile fixture: first comment.
# A continuation of the first comment.
<# Stable global-scope visual tile fixture: second comment. #>

FixtureModuleOne := module {}
FixtureModuleTwo := module:
    FixtureNestedModuleOne := module {}
    FixtureNestedModuleTwo := module {
        FixtureNestedClassOne := class {
            var Counter : int = 0
            Increment(Value : int)<computes> : int = Value + 1
        }
        FixtureNestedClassTwo := class:
            var Score : int = 100
            Decrement(Value : int)<computes> : int = Value - 1
    }

FixtureClassOne := class {}
FixtureClassTwo := class {}

FixtureStructOne := struct {}
FixtureStructTwo := struct {}

FixtureInterfaceOne := interface {}
FixtureInterfaceTwo := interface {}

FixtureEnumOne := enum { First, Second }
FixtureEnumTwo := enum { Alpha, Beta }

FixtureFunctionOne(Input : int)<computes> : int = Input
FixtureFunctionTwo(Input : int)<computes> : int = Input + 1

FixtureConstantOne : string = "one"
FixtureConstantTwo : string = "two"

FixtureTypeAliasOne := type{_Value : int where 0 <= _Value, _Value <= 10}
FixtureTypeAliasTwo := type{_Value : int where 10 <= _Value, _Value <= 20}
