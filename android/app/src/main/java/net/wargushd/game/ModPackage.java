package net.wargushd.game;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * A mod package as a file: one zip whose root is the package's content, mod.json first of all,
 * or one folder that holds exactly that. Reading it is plain file work with no part of the
 * app in it: nothing is written outside the folder it is unpacked into, and a file that is
 * larger or fuller than a package can be is given up.
 */
final class ModPackage {
    static final int OK = 0;
    /** Not a zip, or a zip with nothing in it. */
    static final int NOT_PACKAGE = 1;
    /** No mod.json at the root, nor in the one folder at the root. */
    static final int NO_MANIFEST = 2;
    /** An entry's path could put a file outside the package; detail is the path. */
    static final int BAD_PATH = 3;
    /** A name at the package's root that no package has; detail is the name. */
    static final int UNKNOWN_NAME = 4;
    static final int TOO_LARGE = 5;
    static final int TOO_MANY = 6;
    /** The file broke off, or the storage refused a write. */
    static final int UNREADABLE = 7;
    /** mod.json does not read, or its id is not one. Told by the store, not here. */
    static final int BAD_MANIFEST = 8;

    /** What a package may hold unpacked, and how many entries. */
    static final long MAX_BYTES = 512L * 1024L * 1024L;
    static final int MAX_ENTRIES = 5000;

    /** The names a package's root may hold: the two manifests and the reserved directories. */
    private static final List<String> ROOT_NAMES = Arrays.asList(
        "mod.json", "manifest.json", "defs", "hd", "graphics", "sounds", "strings", "scripts", "maps",
        "card", "editor");

    /** How unpacking ended. */
    static final class Result {
        int fault;
        /** The path or name a refusal is about; empty when it is about the whole file. */
        String detail = "";
        /** The directory that holds mod.json; null unless fault is OK. */
        File root;
    }

    private ModPackage() {}

    static boolean isId(String id) {
        return id != null && id.matches("[a-z0-9-]{3,40}");
    }

    /**
     * True for an entry path that stays inside the package: relative, forward slashes only, no
     * empty part, no ".." anywhere, no drive or control character.
     */
    static boolean isSafePath(String path) {
        if (path == null || path.isEmpty() || path.startsWith("/") || path.endsWith("/")
            || path.contains("..") || path.contains("//")) {
            return false;
        }
        for (int i = 0; i < path.length(); ++i) {
            final char ch = path.charAt(i);
            if (ch < 0x20 || ch == '\\' || ch == ':') {
                return false;
            }
        }
        for (String part : path.split("/")) {
            if (part.equals(".")) {
                return false;
            }
        }
        return true;
    }

    /**
     * Where the package's root is among these entry paths: "" when mod.json is at the zip's
     * root, "name/" when the root holds one folder and mod.json is in it, null when neither.
     */
    static String rootOf(List<String> paths) {
        if (paths.contains("mod.json")) {
            return "";
        }
        String folder = null;
        for (String path : paths) {
            final int cut = path.indexOf('/');
            final String top = (cut < 0) ? path : path.substring(0, cut);
            if (folder != null && !folder.equals(top)) {
                return null;
            }
            folder = top;
        }
        return (folder != null && paths.contains(folder + "/mod.json")) ? folder + "/" : null;
    }

    /** The first name at the package's root that no package has; null when all are known. */
    static String unknownName(List<String> paths, String root) {
        for (String path : paths) {
            if (!path.startsWith(root) || path.length() == root.length()) {
                continue;
            }
            final String inside = path.substring(root.length());
            final int cut = inside.indexOf('/');
            final String top = (cut < 0) ? inside : inside.substring(0, cut);
            if (!ROOT_NAMES.contains(top)) {
                return top;
            }
        }
        return null;
    }

    /** Unpack a package file into an empty directory of its own. The stream is left open. */
    static Result unpack(InputStream file, File into) {
        return unpack(file, into, MAX_BYTES, MAX_ENTRIES);
    }

    static Result unpack(InputStream file, File into, long maxBytes, int maxEntries) {
        final Result r = new Result();
        final List<String> paths = new ArrayList<String>();
        final byte[] buffer = new byte[65536];
        long bytes = 0;
        try {
            final String home = into.getCanonicalPath() + File.separator;
            final ZipInputStream zip = new ZipInputStream(file);
            for (ZipEntry entry; (entry = zip.getNextEntry()) != null; ) {
                final String name = entry.getName();
                final boolean folder = name.endsWith("/");
                final String path = folder ? name.substring(0, name.length() - 1) : name;
                if (paths.size() >= maxEntries) {
                    return fault(r, TOO_MANY, "");
                }
                final File target = new File(into, path);
                // The second look: the place the name really leads to, whatever it is made of.
                if (!isSafePath(path) || !target.getCanonicalPath().startsWith(home)) {
                    return fault(r, BAD_PATH, name);
                }
                paths.add(path);
                if (folder) {
                    if (!target.isDirectory() && !target.mkdirs()) {
                        return fault(r, UNREADABLE, "");
                    }
                    continue;
                }
                final File dir = target.getParentFile();
                if (!dir.isDirectory() && !dir.mkdirs()) {
                    return fault(r, UNREADABLE, "");
                }
                try (OutputStream out = new FileOutputStream(target)) {
                    for (int got; (got = zip.read(buffer)) > 0; ) {
                        bytes += got;
                        if (bytes > maxBytes) {
                            return fault(r, TOO_LARGE, "");
                        }
                        out.write(buffer, 0, got);
                    }
                }
            }
        } catch (IOException | RuntimeException e) {
            // A zip that breaks off, a name that is no text, a full storage.
            return fault(r, paths.isEmpty() ? NOT_PACKAGE : UNREADABLE, "");
        }
        if (paths.isEmpty()) {
            return fault(r, NOT_PACKAGE, "");
        }
        final String root = rootOf(paths);
        if (root == null) {
            return fault(r, NO_MANIFEST, "");
        }
        final String unknown = unknownName(paths, root);
        if (unknown != null) {
            return fault(r, UNKNOWN_NAME, unknown);
        }
        r.root = root.isEmpty() ? into : new File(into, root.substring(0, root.length() - 1));
        if (!new File(r.root, "mod.json").isFile()) {
            return fault(r, NO_MANIFEST, "");
        }
        return r;
    }

    private static Result fault(Result r, int fault, String detail) {
        r.fault = fault;
        r.detail = detail;
        r.root = null;
        return r;
    }

    /** Remove a directory and all in it; true when nothing of it is left. */
    static boolean delete(File dir) {
        final File[] inside = dir.listFiles();
        for (int i = 0; inside != null && i < inside.length; ++i) {
            delete(inside[i]);
        }
        return dir.delete() || !dir.exists();
    }
}
