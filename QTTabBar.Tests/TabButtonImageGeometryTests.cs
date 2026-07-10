using System.Drawing;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace QTTabBarLib.Tests {
    [TestClass]
    public class TabButtonImageGeometryTests {
        [TestMethod]
        public void CloseButtonFrames_SplitsHorizontalFourStateStrip() {
            Rectangle[] frames = TabButtonImageGeometry.GetCloseButtonFrames(new Size(64, 16));

            Assert.AreEqual(4, frames.Length);
            Assert.AreEqual(new Rectangle(32, 0, 16, 16), frames[2]);
        }

        [TestMethod]
        public void CloseButtonFrames_SplitsVerticalFourStateStrip() {
            Rectangle[] frames = TabButtonImageGeometry.GetCloseButtonFrames(new Size(12, 48));

            Assert.AreEqual(4, frames.Length);
            Assert.AreEqual(new Rectangle(0, 36, 12, 12), frames[3]);
        }

        [TestMethod]
        public void CloseButtonFrames_KeepsSingleImageIntact() {
            Rectangle[] frames = TabButtonImageGeometry.GetCloseButtonFrames(new Size(20, 16));

            Assert.AreEqual(1, frames.Length);
            Assert.AreEqual(new Rectangle(0, 0, 20, 16), frames[0]);
        }

        [TestMethod]
        public void FitWithin_DownscalesWithoutUpscaling() {
            Assert.AreEqual(new Size(20, 10),
                    TabButtonImageGeometry.FitWithin(new Size(40, 20), new Size(20, 20)));
            Assert.AreEqual(new Size(8, 8),
                    TabButtonImageGeometry.FitWithin(new Size(8, 8), new Size(20, 20)));
        }

        [TestMethod]
        public void CenterWithin_CentersImageInHitTarget() {
            Assert.AreEqual(new Rectangle(14, 24, 12, 12),
                    TabButtonImageGeometry.CenterWithin(new Rectangle(10, 20, 20, 20), new Size(12, 12)));
        }
    }
}
