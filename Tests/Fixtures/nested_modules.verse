RootModule<public> := module:
    # Every currently recognized definition kind can live in a module.
    NestedModule<internal> := module {
        ChildClass := class {}
        ChildStruct := struct {}
        ChildInterface := interface {}
        ChildEnum := enum { First, Second }
        ChildFunction(Input : int)<computes> : int = Input + 1
        var ChildVariable : int = 1
        ChildConstant : string = "value"
        ChildTypeAlias := type{_Value : int where 0 <= _Value, _Value <= 10}
    }

    ColonNestedModule := module:
        DeepModule := module {}

