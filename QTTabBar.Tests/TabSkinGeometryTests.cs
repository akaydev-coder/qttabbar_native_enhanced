using System.Drawing;
using System.Windows.Forms;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using QTTabBarLib;

namespace QTTabBar.Tests {
    [TestClass]
    public class TabSkinGeometryTests {
        private static int MapCoordinate(int coordinate, int destinationLength,
                int sourceLength, int leadingMargin, int trailingMargin) {
            return TabSkinGeometry.MapNineSliceCoordinate(
                    coordinate,
                    destinationLength,
                    sourceLength,
                    leadingMargin,
                    trailingMargin);
        }

        [TestMethod]
        public void TabAdvance_SubtractsOverlapAndKeepsPositiveWidth() {
            Assert.AreEqual(100, TabSkinGeometry.GetTabAdvance(120, 20));
            Assert.AreEqual(1, TabSkinGeometry.GetTabAdvance(10, 20));
        }

        [TestMethod]
        public void ContentMargins_InsetTheContentRectangle() {
            Rectangle result = TabSkinGeometry.ApplyContentMargins(
                    new Rectangle(0, 0, 120, 40),
                    new Padding(5, 3, 7, 4));
            Assert.AreEqual(new Rectangle(5, 3, 108, 33), result);
        }

        [TestMethod]
        public void ContentMargins_KeepAtLeastOneDrawablePixel() {
            Rectangle result = TabSkinGeometry.ApplyContentMargins(
                    new Rectangle(10, 20, 4, 3),
                    new Padding(99));
            Assert.AreEqual(1, result.Width);
            Assert.AreEqual(1, result.Height);
        }

        [TestMethod]
        public void NineSliceMapping_PreservesFixedEdges() {
            Assert.AreEqual(0, MapCoordinate(0, 100, 20, 4, 4));
            Assert.AreEqual(3, MapCoordinate(3, 100, 20, 4, 4));
            Assert.AreEqual(16, MapCoordinate(96, 100, 20, 4, 4));
            Assert.AreEqual(19, MapCoordinate(99, 100, 20, 4, 4));
        }

        [TestMethod]
        public void NineSliceMapping_ScalesCenterRegion() {
            Assert.AreEqual(10, MapCoordinate(50, 100, 20, 4, 4));
        }

        [TestMethod]
        public void NineSliceMapping_ClampsMarginsAndCoordinates() {
            Assert.AreEqual(0, MapCoordinate(-10, 5, 3, 99, 99));
            Assert.AreEqual(2, MapCoordinate(20, 5, 3, 99, 99));
            Assert.AreEqual(0, MapCoordinate(0, 0, 0, 0, 0));
        }
    }
}
