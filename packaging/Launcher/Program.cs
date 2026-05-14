using System;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Security.Cryptography;

namespace VrpProxyLauncher
{
    internal static class Program
    {
        private const string AppName = "vrpProxy";
        private const string PayloadResourceName = "payload.zip";

        private static void Main()
        {
            byte[] archiveBytes = ReadPayload();
            string hash = ShortHash(archiveBytes);

            string root = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                AppName);
            string target = Path.Combine(root, hash);
            string marker = Path.Combine(target, ".ready");
            string exePath = Path.Combine(target, "vrpProxy.exe");

            Directory.CreateDirectory(root);

            if (!File.Exists(marker) || !File.Exists(exePath)) {
                string temp = Path.Combine(root, "extract-" + Guid.NewGuid().ToString("N"));
                Directory.CreateDirectory(temp);

                using (MemoryStream archiveStream = new MemoryStream(archiveBytes, false))
                using (ZipArchive archive = new ZipArchive(archiveStream, ZipArchiveMode.Read)) {
                    foreach (ZipArchiveEntry entry in archive.Entries) {
                        string destinationPath = Path.Combine(temp, entry.FullName);
                        string destinationDirectory = Path.GetDirectoryName(destinationPath);
                        if (!string.IsNullOrEmpty(destinationDirectory)) {
                            Directory.CreateDirectory(destinationDirectory);
                        }

                        if (string.IsNullOrEmpty(entry.Name)) {
                            Directory.CreateDirectory(destinationPath);
                        } else {
                            entry.ExtractToFile(destinationPath, true);
                        }
                    }
                }

                if (Directory.Exists(target)) {
                    Directory.Delete(target, true);
                }

                Directory.Move(temp, target);
                File.WriteAllText(marker, DateTimeOffset.UtcNow.ToString("O"));
            }

            ProcessStartInfo startInfo = new ProcessStartInfo();
            startInfo.FileName = exePath;
            startInfo.WorkingDirectory = target;
            startInfo.UseShellExecute = false;

            Process.Start(startInfo);
        }

        private static byte[] ReadPayload()
        {
            Assembly assembly = Assembly.GetExecutingAssembly();
            using (Stream payload = assembly.GetManifestResourceStream(PayloadResourceName)) {
                if (payload == null) {
                    throw new InvalidOperationException("Embedded payload.zip was not found.");
                }

                using (MemoryStream memory = new MemoryStream()) {
                    payload.CopyTo(memory);
                    return memory.ToArray();
                }
            }
        }

        private static string ShortHash(byte[] bytes)
        {
            using (SHA256 sha256 = SHA256.Create()) {
                byte[] hash = sha256.ComputeHash(bytes);
                return BitConverter.ToString(hash).Replace("-", "").Substring(0, 12).ToLowerInvariant();
            }
        }
    }
}
