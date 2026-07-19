#include "Lustre/Resolver.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <unordered_map>

namespace Lustre {

namespace {

struct VariableScope {
    std::unordered_map<std::string, ValuePart> Values;
};

void CollectVariablesFromRules(const std::vector<RulePtr>& Rules, std::unordered_map<std::string, ValuePart>& Out) {
    for (const auto& R : Rules) {
        for (const auto& V : R->Variables) {
            Out[V.Name] = V.Value;
        }
        CollectVariablesFromRules(R->NestedRules, Out);
    }
}

// §1.4: a component-scoped --variable shadows a same-named global one,
// within that component's own file only.
VariableScope BuildVariableScope(const Stylesheet* Global, const Stylesheet* Component) {
    VariableScope Scope;
    if (Global && Global->Root) {
        for (const auto& V : Global->Root->Variables) {
            Scope.Values[V.Name] = V.Value;
        }
    }
    if (Component) {
        if (Component->Root) {
            for (const auto& V : Component->Root->Variables) {
                Scope.Values[V.Name] = V.Value;
            }
        }
        CollectVariablesFromRules(Component->Rules, Scope.Values);
    }
    return Scope;
}

// Resolves a var(--name) reference through the scope. Returns nullptr (and
// records a diagnostic) if the referenced variable was never declared --
// §1.4/§6: no fallback form exists, an undefined reference is always an
// error.
const ValuePart* ResolveVariableRef(const ValuePart& Part, const VariableScope& Scope,
                                     std::vector<ResolveDiagnostic>& Diagnostics) {
    if (!Part.VariableRef) {
        return &Part;
    }
    auto It = Scope.Values.find(*Part.VariableRef);
    if (It == Scope.Values.end()) {
        Diagnostics.push_back(ResolveDiagnostic{"Undefined variable `var(--" + *Part.VariableRef + ")` -- no `--" +
                                                  *Part.VariableRef + "` is declared globally or in this component's scope."});
        return nullptr;
    }
    // A variable's own value can't itself be another var() reference in any
    // example the spec gives; if it somehow is, resolve one level deep and
    // stop there rather than looping.
    if (It->second.VariableRef) {
        return ResolveVariableRef(It->second, Scope, Diagnostics);
    }
    return &It->second;
}

std::string StripUnitSuffix(std::string_view Text, float& OutValue) {
    std::size_t End = Text.size();
    while (End > 0 && !(std::isdigit(static_cast<unsigned char>(Text[End - 1])))) {
        --End;
    }
    const std::string_view NumericPart = Text.substr(0, End);
    OutValue = 0.0F;
    std::from_chars(NumericPart.data(), NumericPart.data() + NumericPart.size(), OutValue);
    return std::string(Text.substr(End));
}

// Folds px/%/vw/vh/rem/em (§1.5) down to a single logical-pixel float, the
// shape the IR (§3) stores. %/vw/vh proper resolution needs parent-size/
// window-size context this host-agnostic core doesn't have piped in yet, and
// rem/em are stubbed pending a root-font-size convention (§1.5/§7 open
// question) -- all five pass the bare numeric value through unscaled, same
// "accepted, not yet fully applied" treatment the spec gives width/height.
std::optional<float> ParseLength(const ValuePart& Part) {
    if (!Part.Number) {
        return std::nullopt;
    }
    float Value = 0.0F;
    StripUnitSuffix(*Part.Number, Value);
    return Value;
}

std::optional<float> ParseSeconds(const ValuePart& Part) {
    if (!Part.Number) {
        return std::nullopt;
    }
    std::string_view Text = *Part.Number;
    if (!Text.empty() && Text.back() == 's') {
        Text.remove_suffix(1);
    }
    float Value = 0.0F;
    std::from_chars(Text.data(), Text.data() + Text.size(), Value);
    return Value;
}

std::optional<Color> ParseColor(const ValuePart& Part) {
    if (!Part.HexColor) {
        return std::nullopt;
    }
    std::string_view Hex = *Part.HexColor;
    if (!Hex.empty() && Hex.front() == '#') {
        Hex.remove_prefix(1);
    }
    auto HexPair = [](std::string_view S, std::size_t Index) -> std::uint8_t {
        if (Index + 1 >= S.size()) {
            return 0;
        }
        unsigned Value = 0;
        std::from_chars(S.data() + Index, S.data() + Index + 2, Value, 16);
        return static_cast<std::uint8_t>(Value);
    };
    auto HexNibble = [](std::string_view S, std::size_t Index) -> std::uint8_t {
        if (Index >= S.size()) {
            return 0;
        }
        unsigned Value = 0;
        std::from_chars(S.data() + Index, S.data() + Index + 1, Value, 16);
        return static_cast<std::uint8_t>(Value * 17); // nibble-doubled, e.g. 'E' -> 0xEE
    };
    Color C;
    if (Hex.size() == 3) {
        C.R = HexNibble(Hex, 0);
        C.G = HexNibble(Hex, 1);
        C.B = HexNibble(Hex, 2);
        C.A = 255;
    } else if (Hex.size() == 6) {
        C.R = HexPair(Hex, 0);
        C.G = HexPair(Hex, 2);
        C.B = HexPair(Hex, 4);
        C.A = 255;
    } else if (Hex.size() == 8) {
        C.R = HexPair(Hex, 0);
        C.G = HexPair(Hex, 2);
        C.B = HexPair(Hex, 4);
        C.A = HexPair(Hex, 6);
    } else {
        return std::nullopt;
    }
    return C;
}

std::optional<EdgeInsets> ParseEdgeInsets(const std::vector<float>& Lengths) {
    // CSS 1-4 value shorthand (§2's `padding`/`margin`).
    switch (Lengths.size()) {
    case 1: return EdgeInsets{Lengths[0], Lengths[0], Lengths[0], Lengths[0]};
    case 2: return EdgeInsets{Lengths[1], Lengths[0], Lengths[1], Lengths[0]};
    case 3: return EdgeInsets{Lengths[1], Lengths[0], Lengths[1], Lengths[2]};
    case 4: return EdgeInsets{Lengths[3], Lengths[0], Lengths[1], Lengths[2]};
    default: return std::nullopt;
    }
}

// State threaded through one target's declaration application pass, since
// `font-family`/`font-size` combine into a single FontRequest (§2) and may
// arrive as separate declarations in either order.
struct FontAccumulator {
    std::optional<std::string> Path;
    std::optional<float>       SizeLogical;
};

void ApplyDeclaration(const Declaration& Decl, const VariableScope& Scope, ResolvedStyle& Out,
                       FontAccumulator& Font, std::vector<ResolveDiagnostic>& Diagnostics) {
    std::vector<ValuePart> Resolved;
    Resolved.reserve(Decl.Values.size());
    for (const auto& Raw : Decl.Values) {
        if (const ValuePart* R = ResolveVariableRef(Raw, Scope, Diagnostics)) {
            Resolved.push_back(*R);
        }
    }
    if (Resolved.empty()) {
        return;
    }

    const std::string& Prop = Decl.Property;

    if (Prop == "background-color") {
        Out.BackgroundColor = ParseColor(Resolved[0]);
    } else if (Prop == "border-color") {
        Out.BorderColor = ParseColor(Resolved[0]);
    } else if (Prop == "border-width") {
        Out.BorderWidth = ParseLength(Resolved[0]);
    } else if (Prop == "border-radius") {
        Out.BorderRadius = ParseLength(Resolved[0]);
    } else if (Prop == "padding" || Prop == "margin") {
        std::vector<float> Lengths;
        for (const auto& V : Resolved) {
            if (auto L = ParseLength(V)) {
                Lengths.push_back(*L);
            }
        }
        (Prop == "padding" ? Out.Padding : Out.Margin) = ParseEdgeInsets(Lengths);
    } else if (Prop == "color") {
        Out.TextColor = ParseColor(Resolved[0]);
    } else if (Prop == "font-family") {
        if (Resolved[0].StringValue) {
            Font.Path = Resolved[0].StringValue;
        }
    } else if (Prop == "font-size") {
        Font.SizeLogical = ParseLength(Resolved[0]);
    } else if (Prop == "display") {
        if (Resolved[0].Literal == "stack") {
            Out.DisplayMode = Display::Stack;
        } else if (Resolved[0].Literal == "inline") {
            Out.DisplayMode = Display::Inline;
        }
    } else if (Prop == "flex-direction") {
        if (Resolved[0].Literal == "row") {
            Out.FlexDirectionMode = FlexDirection::Row;
        } else if (Resolved[0].Literal == "column") {
            Out.FlexDirectionMode = FlexDirection::Column;
        }
    } else if (Prop == "gap") {
        Out.Gap = ParseLength(Resolved[0]);
    } else if (Prop == "align-items") {
        static const std::unordered_map<std::string, Align> kAligns{
            {"start", Align::Start}, {"center", Align::Center}, {"end", Align::End}, {"stretch", Align::Stretch}};
        if (Resolved[0].Literal) {
            auto It = kAligns.find(*Resolved[0].Literal);
            if (It != kAligns.end()) {
                Out.AlignItems = It->second;
            }
        }
    } else if (Prop == "width") {
        Out.WidthLogical = ParseLength(Resolved[0]);
    } else if (Prop == "height") {
        Out.HeightLogical = ParseLength(Resolved[0]);
    } else if (Prop == "transform") {
        if (Resolved[0].Literal == "scale" && Resolved[0].CallArgument) {
            float Value = 0.0F;
            std::from_chars(Resolved[0].CallArgument->data(),
                             Resolved[0].CallArgument->data() + Resolved[0].CallArgument->size(), Value);
            Out.TransformScale = Value;
        }
    } else if (Prop == "transition") {
        ColorTransition Transition;
        for (const auto& V : Resolved) {
            if (V.Literal) {
                Transition.Property = *V.Literal;
            } else if (auto Seconds = ParseSeconds(V)) {
                Transition.DurationSeconds = *Seconds;
            }
        }
        if (!Transition.Property.empty()) {
            Out.Transition = Transition;
        }
    }
    // Unknown properties are silently ignored -- v1 does no
    // property-applicability validation (§6, "Deliberately not diagnosed").
}

ResolvedStyle& OverlayFor(ResolvedStyle& Base, PseudoKind Kind) {
    std::shared_ptr<ResolvedStyle>* Slot = nullptr;
    switch (Kind) {
    case PseudoKind::Hover: Slot = &Base.Hover; break;
    case PseudoKind::Active: Slot = &Base.Active; break;
    case PseudoKind::Disabled: Slot = &Base.Disabled; break;
    }
    if (!*Slot) {
        *Slot = std::make_shared<ResolvedStyle>();
    }
    return **Slot;
}

void ApplyRuleDeclarations(const Rule& R, const VariableScope& Scope, ResolvedStyle& Out,
                            std::vector<ResolveDiagnostic>& Diagnostics) {
    ResolvedStyle& Target = R.Pseudo ? OverlayFor(Out, *R.Pseudo) : Out;
    FontAccumulator Font;
    for (const auto& Decl : R.Declarations) {
        ApplyDeclaration(Decl, Scope, Target, Font, Diagnostics);
    }
    if (Font.Path && Font.SizeLogical) {
        Target.Font = FontRequest{*Font.Path, *Font.SizeLogical};
    }
}

bool SelectorHeadMatches(const Rule& R, const IStyleTarget& Element) {
    if (R.Kind == SelectorKind::Class) {
        return !R.ClassName.empty() && Element.ClassName() == R.ClassName;
    }
    const std::string_view MappedTag = PrimitiveTagForSelector(R.PrimitiveName);
    return !MappedTag.empty() && Element.PrimitiveTag() == MappedTag;
}

struct MatchedRule {
    const Rule* R;
};

// Builds the boundary-respecting ancestor chain for descendant matching
// (§1.2): index 0 is Target itself, each following index is one step
// further up. The walk stops (excludes anything past it) at the first node
// where IsComponentRoot() is true, unless Unbounded (global.lustre has no
// "own subtree" to be bounded by).
std::vector<const IStyleTarget*> BuildAncestorChain(const IStyleTarget& Target, bool Unbounded) {
    std::vector<const IStyleTarget*> Chain{&Target};
    if (Target.IsComponentRoot() && !Unbounded) {
        return Chain;
    }
    const IStyleTarget* Node = Target.Parent();
    while (Node) {
        Chain.push_back(Node);
        if (Node->IsComponentRoot() && !Unbounded) {
            break;
        }
        Node = Node->Parent();
    }
    return Chain;
}

// Walks one rule (and its nested descendant selectors) against the ancestor
// chain, collecting every (Rule) whose full selector chain matches Target
// (chain[0]). BoundExclusive restricts how far up the chain this node's own
// selector may match -- strictly closer to the target than whatever
// ancestor its own lexical parent selector matched, giving real descendant
// semantics ("at any depth" below that ancestor, never above it).
void CollectMatches(const Rule& Node, const std::vector<const IStyleTarget*>& Chain, std::size_t BoundExclusive,
                     std::vector<MatchedRule>& Out) {
    for (std::size_t I = 0; I < BoundExclusive && I < Chain.size(); ++I) {
        if (!SelectorHeadMatches(Node, *Chain[I])) {
            continue;
        }
        if (I == 0) {
            Out.push_back(MatchedRule{&Node});
        }
        for (const auto& Child : Node.NestedRules) {
            CollectMatches(*Child, Chain, I, Out);
        }
    }
}

void ApplyLayer(const Stylesheet* Sheet, const IStyleTarget& Target, bool Unbounded, const VariableScope& Scope,
                 ResolvedStyle& Out, std::vector<ResolveDiagnostic>& Diagnostics) {
    if (!Sheet) {
        return;
    }
    const std::vector<const IStyleTarget*> Chain = BuildAncestorChain(Target, Unbounded);

    std::vector<MatchedRule> Matches;
    for (const auto& TopRule : Sheet->Rules) {
        CollectMatches(*TopRule, Chain, Chain.size(), Matches);
    }

    std::stable_sort(Matches.begin(), Matches.end(), [](const MatchedRule& A, const MatchedRule& B) {
        if (A.R->Depth != B.R->Depth) {
            return A.R->Depth < B.R->Depth;
        }
        return A.R->SourceIndex < B.R->SourceIndex;
    });

    for (const auto& Match : Matches) {
        ApplyRuleDeclarations(*Match.R, Scope, Out, Diagnostics);
    }
}

} // namespace

std::string_view PrimitiveTagForSelector(std::string_view LustreSelectorName) {
    static const std::unordered_map<std::string_view, std::string_view> kMapping{
        {"frame", "Frame"}, {"inline", "Inline"}, {"grid", "Grid"}, {"image", "Image"}, {"text", "Text"}};
    auto It = kMapping.find(LustreSelectorName);
    return It == kMapping.end() ? std::string_view{} : It->second;
}

ResolvedStyle Resolver::Resolve(const IStyleTarget& Target, const StylesheetSet& Sheets, bool Unbounded,
                                 std::vector<ResolveDiagnostic>& OutDiagnostics) const {
    const VariableScope Scope = BuildVariableScope(Sheets.Global, Sheets.Component);

    ResolvedStyle Style;
    // §1.3: exactly two cascade layers -- global.lustre first, then the
    // component's own file overrides it for anything both define. Applied
    // as two fully independent passes (not merged by specificity across
    // layers) so the component layer always wins regardless of nesting
    // depth on either side.
    ApplyLayer(Sheets.Global, Target, Unbounded, Scope, Style, OutDiagnostics);
    ApplyLayer(Sheets.Component, Target, Unbounded, Scope, Style, OutDiagnostics);
    return Style;
}

} // namespace Lustre
