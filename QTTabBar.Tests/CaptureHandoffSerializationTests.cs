using System;
using System.IO;
using System.Reflection;
using System.Runtime.Serialization.Formatters.Binary;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using QTTabBarLib;

namespace QTTabBar.Tests {
    [TestClass]
    public class CaptureHandoffSerializationTests {
        [TestMethod]
        public void CaptureRequest_RoundTripsWithoutSerializingUiDelegateGraph() {
            const string expectedPath = @"C:\capture target";
            const string expectedSelection = @"C:\capture target\selected.txt";
            Assembly assembly = typeof(SerializeDelegate).Assembly;
            Type requestType = assembly.GetType("QTTabBarLib.CaptureHandoffRequest", true);
            Type kindType = assembly.GetType("QTTabBarLib.CaptureHandoffKind", true);
            object expectedKind = Enum.Parse(kindType, "OpenTabAndSelect");
            object request = Activator.CreateInstance(requestType,
                    BindingFlags.Instance | BindingFlags.NonPublic, null,
                    new object[] { expectedKind, expectedPath, expectedSelection, true }, null);
            MethodInfo queueMethod = requestType.GetMethod("TryQueueOnMain",
                    BindingFlags.Instance | BindingFlags.NonPublic);

            Func<bool> restoredRequest = RoundTrip((Func<bool>)Delegate.CreateDelegate(
                    typeof(Func<bool>), request, queueMethod));

            Assert.IsNotNull(restoredRequest);
            object restoredTarget = restoredRequest.Target;
            Assert.IsNotNull(restoredTarget);
            Assert.AreEqual(requestType, restoredTarget.GetType());
            Assert.AreEqual(expectedKind, GetProperty(restoredTarget, "Kind"));
            Assert.AreEqual(expectedPath, GetProperty(restoredTarget, "Path"));
            Assert.AreEqual(expectedSelection, GetProperty(restoredTarget, "Selection"));
            Assert.AreEqual(true, GetProperty(restoredTarget, "WaitForSelection"));
        }

        private static object GetProperty(object target, string name) {
            return target.GetType().GetProperty(name,
                    BindingFlags.Instance | BindingFlags.NonPublic).GetValue(target, null);
        }

        private static Func<bool> RoundTrip(Func<bool> request) {
            using(MemoryStream stream = new MemoryStream()) {
                BinaryFormatter formatter = new BinaryFormatter();
                formatter.Serialize(stream, new SerializeDelegate(request));
                stream.Position = 0;
                SerializeDelegate restored = (SerializeDelegate)formatter.Deserialize(stream);
                return restored.Delegate as Func<bool>;
            }
        }
    }
}
