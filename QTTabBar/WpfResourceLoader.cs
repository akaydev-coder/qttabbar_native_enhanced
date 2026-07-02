using System;
using System.Reflection;
using System.Windows;

namespace QTTabBarLib {
    internal static class WpfResourceLoader {
        private static readonly string ComponentPrefix = BuildComponentPrefix();

        internal static void LoadComponent(object component, string resourcePath) {
            if(component == null) throw new ArgumentNullException("component");
            if(String.IsNullOrEmpty(resourcePath)) throw new ArgumentNullException("resourcePath");

            Uri resourceLocator = new Uri(
                    ComponentPrefix + resourcePath.TrimStart('/'),
                    UriKind.Relative);
            Application.LoadComponent(component, resourceLocator);
        }

        private static string BuildComponentPrefix() {
            AssemblyName assemblyName = typeof(WpfResourceLoader).Assembly.GetName();
            byte[] tokenBytes = assemblyName.GetPublicKeyToken();
            string token = tokenBytes == null
                    ? String.Empty
                    : BitConverter.ToString(tokenBytes).Replace("-", String.Empty).ToLowerInvariant();
            string identity = "/" + assemblyName.Name + ";v" + assemblyName.Version;
            if(token.Length != 0) {
                identity += ";" + token;
            }
            return identity + ";component/";
        }
    }
}
