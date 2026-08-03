# Formatting fixture: line comment.
<# Formatting fixture: block comment. #>

FormattingBrace := class { Value : int = 1; Other:int=2 }

FormattingColon := class:
    Value : int = 1
    Other : int = 2

FormattingFunction(Input : int)<computes> : int =
    Local : int = Abs( Input )

    ((Local + 1))

FormattingCondition(Value: float, Low: float, High: float)<computes> : float =
    if:
        Denom: float = High - Low
        Denom <> 0.0
    then:
        Value / Denom
    else:
        Value

FormattingParenthesizedCondition(Input: logic) : void =
    if (Input?) {}
