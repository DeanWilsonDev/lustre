#pragma once

#include "Lustre/Ast.h"
#include "Lustre/ResolvedStyle.h"

#include <string>
#include <string_view>
#include <vector>

namespace Lustre {

// Backend/host-agnostic view onto one element of whatever real render tree
// the host owns (an Iris `IrisComponent`/mounted-widget tree, in practice).
// A host adapter implements this over its own node type; the resolver never
// depends on Iris or any backend directly (docs/lustre_core_spec.md §0 goal
// 4). Mirrors the abstraction-point pattern `iris`'s own
// IHostLanguageTokenizer already uses for the same reason.
class IStyleTarget {
public:
    virtual ~IStyleTarget() = default;

    // The element's `class` prop value; empty if none was set.
    virtual std::string ClassName() const = 0;

    // The element's real Iris tag, PascalCase ("Frame", "Inline", "Grid",
    // "Image", "Text") -- compared against §1.1's mapping table.
    virtual std::string PrimitiveTag() const = 0;

    // True exactly for the root element of the component instance currently
    // being styled. Descendant-selector ancestor walks (§1.2) stop climbing
    // once they reach a node where this is true -- a component's own
    // stylesheet can select down to and including its own root, never past
    // it. Callers resolving `global.lustre` (which has no "own subtree" to
    // be bounded by, §1.2) should pass Unbounded=true to Resolve() below,
    // which makes the walk ignore this entirely.
    virtual bool IsComponentRoot() const = 0;

    virtual const IStyleTarget* Parent() const = 0;
};

// The two-layer cascade (§1.3): a stylesheet parsed from global.lustre, and
// the component's own paired Name.lustre. Either may be null (a project
// might have no global.lustre yet; not every component needs its own
// styles).
struct StylesheetSet {
    const Stylesheet* Global{nullptr};
    const Stylesheet* Component{nullptr};
};

struct ResolveDiagnostic {
    std::string Message;
};

// Parses, cascades, and resolves variables/selectors down to a concrete
// ResolvedStyle for one element. Stateless beyond its inputs -- safe to
// reuse across many Resolve() calls.
class Resolver {
public:
    // Unbounded should be true only when Sheets.Component *is* global.lustre
    // itself (i.e. Target's whole subtree is being matched against
    // global.lustre, which has no component-boundary restriction, §1.2).
    // For an ordinary component's own stylesheet, leave it false so
    // ancestor walks stop at Target's own component root.
    ResolvedStyle Resolve(const IStyleTarget& Target, const StylesheetSet& Sheets, bool Unbounded,
                           std::vector<ResolveDiagnostic>& OutDiagnostics) const;
};

// §1.1's closed mapping table, exposed for callers that need it directly
// (e.g. a host adapter building an IStyleTarget). Returns "" for a name
// outside the five Core primitives.
std::string_view PrimitiveTagForSelector(std::string_view LustreSelectorName);

} // namespace Lustre
