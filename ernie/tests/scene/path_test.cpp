#include "ernie/scene/path.h"

#include <gtest/gtest.h>

#include <unordered_set>

using ernie::Path;

TEST(Path, ParsesAbsolutePathsOfIdentifiers) {
    EXPECT_EQ(Path("/World/Sun_2").String(), "/World/Sun_2");
    EXPECT_TRUE(Path("/").IsRoot());
    EXPECT_TRUE(Path().IsEmpty());
    for (const char* bad : {"", "World", "/World/", "//a", "/2a", "/a b", "/a-b", "/a/./b"}) {
        EXPECT_THROW(Path{bad}, std::invalid_argument) << bad;
    }
    EXPECT_TRUE(Path::IsValidName("_x9"));
    EXPECT_FALSE(Path::IsValidName(""));
}

TEST(Path, NavigatesNamesParentsAndChildren) {
    const Path sun("/World/Sun");
    EXPECT_EQ(sun.Name(), "Sun");
    EXPECT_EQ(sun.Parent(), Path("/World"));
    EXPECT_EQ(Path("/World").Parent(), Path::Root());
    EXPECT_TRUE(Path::Root().Parent().IsEmpty());
    EXPECT_TRUE(Path().Parent().IsEmpty());
    EXPECT_EQ(Path::Root().Name(), "");
    EXPECT_EQ(Path::Root().Child("World"), Path("/World"));
    EXPECT_EQ(Path("/World").Child("Sun"), sun);
    EXPECT_THROW(Path().Child("x"), std::invalid_argument);
    EXPECT_THROW(sun.Child("a/b"), std::invalid_argument);
}

TEST(Path, PrefixesRespectNameBoundaries) {
    EXPECT_TRUE(Path("/World/Sun").HasPrefix(Path("/World")));
    EXPECT_TRUE(Path("/World").HasPrefix(Path("/World")));
    EXPECT_TRUE(Path("/World").HasPrefix(Path::Root()));
    EXPECT_FALSE(Path("/Worlds").HasPrefix(Path("/World")));
    EXPECT_FALSE(Path("/World").HasPrefix(Path()));
    EXPECT_FALSE(Path().HasPrefix(Path::Root()));
}

TEST(Path, OrdersDescendantsRightAfterTheirAncestorAndHashes) {
    EXPECT_LT(Path("/a"), Path("/a/b"));
    EXPECT_LT(Path("/a/z"), Path("/a0"));
    EXPECT_LT(Path("/a/z"), Path("/a_"));
    const std::unordered_set<Path> set{Path("/a"), Path("/a"), Path("/b")};
    EXPECT_EQ(set.size(), 2u);
}
