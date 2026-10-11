// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Hierarchical gameplay tags and ref-counted containers.

using Aver.Framework;

static class GameplayTagTests
{
    public static int RunAll() => T.Suite(() =>
    {
        T.Run("tag names: grammar accepted and rejected", Grammar);
        T.Run("tags: requesting A.B.C interns A and A.B", ParentsInterned);
        T.Run("tags: Matches is hierarchical, MatchesExact is not", Matching);
        T.Run("container: counts floor at zero", CountsFloor);
        T.Run("container: Has is hierarchical, HasExact is not", HierarchicalHas);
        T.Run("container: presence flips are reported for the tag and its ancestors", Flips);
        T.Run("container: HasAny / HasAll / HasNone", SetQueries);
        T.Run("tags: ids are sequential, stable and ordered by registration", IdOrder);
        T.Run("tags: a read-only view refuses mutation", LockedView);
    });

    private static void Grammar()
    {
        T.Check("State.Stunned is valid", TagRegistry.IsValidName("State.Stunned"));
        T.Check("a single segment is valid", TagRegistry.IsValidName("Alive"));
        T.Check("digits and underscores after the first letter", TagRegistry.IsValidName("A1.b_2"));
        T.Check("8 segments is the limit", TagRegistry.IsValidName("A.B.C.D.E.F.G.H"));
        T.Check("9 segments is refused", !TagRegistry.IsValidName("A.B.C.D.E.F.G.H.I"));
        T.Check("a leading digit is refused", !TagRegistry.IsValidName("1abc"));
        T.Check("an empty segment is refused", !TagRegistry.IsValidName("A..B"));
        T.Check("a trailing dot is refused", !TagRegistry.IsValidName("A."));
        T.Check("a leading dot is refused", !TagRegistry.IsValidName(".A"));
        T.Check("a hyphen is refused", !TagRegistry.IsValidName("A-B"));
        T.Check("empty is refused", !TagRegistry.IsValidName(""));
        T.Check("128 characters is the limit", TagRegistry.IsValidName(new string('a', 128)));
        T.Check("129 characters is refused", !TagRegistry.IsValidName(new string('a', 129)));
        T.Reset();
        T.Check("Request of a bad name returns None", !GameplayTag.Request("not valid").IsValid);
        T.Check("...and warns", T.WarnCount("invalid gameplay tag") == 1);
    }

    private static void ParentsInterned()
    {
        GameplayTag c = GameplayTag.Request("Tpar.B.C");
        T.Check("Tpar was interned", GameplayTag.TryFind("Tpar", out GameplayTag a));
        T.Check("Tpar.B was interned", GameplayTag.TryFind("Tpar.B", out GameplayTag b));
        T.Check("parent chain", c.Parent == b && b.Parent == a && !a.Parent.IsValid);
        T.Check("depths", c.Depth == 3 && b.Depth == 2 && a.Depth == 1);
        T.Check("names", c.Name == "Tpar.B.C");
        T.Check("TryFind does not intern", !GameplayTag.TryFind("Tpar.Never", out _) && !GameplayTag.TryFind("Tpar.Never", out _));
        T.Check("Request is idempotent", GameplayTag.Request("Tpar.B.C") == c);
        T.Check("FromId round-trips", GameplayTag.FromId(c.Id) == c && !GameplayTag.FromId(0).IsValid && !GameplayTag.FromId(1 << 20).IsValid);
    }

    private static void Matching()
    {
        GameplayTag parent = GameplayTag.Request("Tm.State.Stunned");
        GameplayTag child = GameplayTag.Request("Tm.State.Stunned.Hard");
        GameplayTag root = GameplayTag.Request("Tm");
        T.Check("a child matches its parent", child.Matches(parent));
        T.Check("a child matches the root", child.Matches(root));
        T.Check("a parent does not match its child", !parent.Matches(child));
        T.Check("a tag matches itself", parent.Matches(parent));
        T.Check("MatchesExact: same tag", parent.MatchesExact(parent));
        T.Check("MatchesExact: not a descendant", !child.MatchesExact(parent));
        T.Check("None matches nothing", !GameplayTag.None.Matches(parent) && !parent.Matches(GameplayTag.None));
    }

    private static void CountsFloor()
    {
        GameplayTag t = GameplayTag.Request("Tc.Count");
        var c = new GameplayTagContainer();
        c.Add(t, 2);
        T.Check("added twice", c.CountOf(t) == 2);
        c.Remove(t, 1);
        T.Check("removed once", c.CountOf(t) == 1 && c.Has(t));
        c.Remove(t, 5);
        T.Check("over-removal floors at 0", c.CountOf(t) == 0 && !c.Has(t) && c.Count == 0);
        c.Remove(t, 1);
        T.Check("removing an absent tag stays at 0", c.CountOf(t) == 0);
    }

    private static void HierarchicalHas()
    {
        GameplayTag stunned = GameplayTag.Request("Th.State.Stunned");
        GameplayTag hard = GameplayTag.Request("Th.State.Stunned.Hard");
        var c = new GameplayTagContainer(hard);
        T.Check("holding Hard satisfies Stunned", c.Has(stunned));
        T.Check("...but Stunned is not held exactly", !c.HasExact(stunned));
        T.Check("Hard is held exactly", c.HasExact(hard));
        T.Check("the reverse does not hold", !new GameplayTagContainer(stunned).Has(hard));
        c.Remove(hard);
        T.Check("removing Hard drops the implied ancestors", !c.Has(stunned));
    }

    private static void Flips()
    {
        GameplayTag b = GameplayTag.Request("Tf.A.B");
        GameplayTag cc = GameplayTag.Request("Tf.A.C");
        var c = new GameplayTagContainer();
        var flips = new List<TagFlip>();
        c.MutateInternal(b, 1, flips);
        T.Check("first add flips the tag and both ancestors", flips.Count == 3 && flips.All(f => f.Present));
        flips.Clear();
        c.MutateInternal(b, 1, flips);
        T.Check("a second reference flips nothing", flips.Count == 0);
        c.MutateInternal(cc, 1, flips);
        T.Check("a sibling flips only itself (ancestors already present)", flips.Count == 1 && flips[0].Tag == cc);
        flips.Clear();
        c.MutateInternal(b, -2, flips);
        T.Check("removing the last B reference flips B only (the sibling keeps the ancestors)", flips.Count == 1 && flips[0].Tag == b && !flips[0].Present);
        flips.Clear();
        c.MutateInternal(cc, -1, flips);
        T.Check("removing the last tag flips C and both ancestors off", flips.Count == 3 && flips.All(f => !f.Present));
    }

    private static void SetQueries()
    {
        GameplayTag a = GameplayTag.Request("Tq.A");
        GameplayTag b = GameplayTag.Request("Tq.B");
        GameplayTag x = GameplayTag.Request("Tq.X");
        var held = new GameplayTagContainer(a, b);
        T.Check("HasAny: one of two", held.HasAny(new GameplayTagContainer(a, x)));
        T.Check("HasAny: none", !held.HasAny(new GameplayTagContainer(x)));
        T.Check("HasAny: empty argument is false", !held.HasAny(new GameplayTagContainer()));
        T.Check("HasAll: all present", held.HasAll(new GameplayTagContainer(a, b)));
        T.Check("HasAll: one missing", !held.HasAll(new GameplayTagContainer(a, x)));
        T.Check("HasAll: empty argument is true", held.HasAll(new GameplayTagContainer()));
        T.Check("HasNone", held.HasNone(new GameplayTagContainer(x)) && !held.HasNone(new GameplayTagContainer(a)));
    }

    private static void IdOrder()
    {
        GameplayTag one = GameplayTag.Request("Tid.Zeta.One");
        GameplayTag two = GameplayTag.Request("Tid.Alpha.Two");
        T.Check("registration order, not alphabetical order", one.Id < two.Id);
        T.Check("parents come before children", one.Parent.Id < one.Id);
        T.Check("ids are sequential for a fresh chain", two.Parent.Id == one.Id + 1 && two.Id == one.Id + 2);
        T.Check("a repeat request keeps the id", GameplayTag.Request("Tid.Zeta.One").Id == one.Id);
        IReadOnlyList<GameplayTag> all = GameplayTag.All;
        T.Check("All lists tags in id order", all.Zip(all.Skip(1), (p, q) => p.Id < q.Id).All(ok => ok));
        var c = new GameplayTagContainer(two, one);
        T.Check("a container enumerates in id order", c.First() == one && c.Last() == two);
    }

    private static void LockedView()
    {
        T.Reset();
        AbilitySystem s = AbilitySystem.GetOrAdd(T.E(1));
        bool threw = false;
        try { s.Tags.Add(GameplayTag.Request("Tl.X")); } catch (InvalidOperationException) { threw = true; }
        T.Check("AbilitySystem.Tags is read-only", threw);
    }
}
