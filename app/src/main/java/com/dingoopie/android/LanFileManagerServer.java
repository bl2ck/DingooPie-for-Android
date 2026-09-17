package com.dingoopie.android;

import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.UriPermission;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.util.Log;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.ByteArrayOutputStream;
import java.io.Closeable;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FilterOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.URLDecoder;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.Enumeration;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;

final class LanFileManagerServer implements Closeable {
    private static final String TAG = "DingooPieFileManager";
    private static final int PREFERRED_PORT = 8080;
    private static final int MAX_HEADER_BYTES = 64 * 1024;
    private static final long MAX_UPLOAD_BYTES = 2L * 1024L * 1024L * 1024L;
    private static final int TOKEN_LENGTH = 6;
    private static final String TOKEN_ALPHABET =
            "0123456789abcdefghijklmnopqrstuvwxyz";
    private static final String DIRECTORY_MIME_TYPE =
            DocumentsContract.Document.MIME_TYPE_DIR;
    private static final String ICON_UPLOAD =
            "<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M12 16V4M7 9l5-5 5 5'/><path d='M5 15v5h14v-5'/></svg>";
    private static final String ICON_FILE_SELECT =
            "<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M8 12.5l6-6a3 3 0 014 4.2l-8 8a5 5 0 01-7-7l7.5-7.5'/></svg>";
    private static final String ICON_DOWNLOAD =
            "<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M12 4v12M7 11l5 5 5-5'/><path d='M5 15v5h14v-5'/></svg>";
    private static final String ICON_RENAME =
            "<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M4 20l4.5-1 10-10L15 5.5l-10 10z'/><path d='M13.5 6.5l4 4'/></svg>";
    private static final String ICON_DELETE =
            "<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M4 7h16M9 7V4h6v3M7 7l1 13h8l1-13'/><path d='M10 11v5M14 11v5'/></svg>";
    private static final String ICON_GAME_FILE =
            "<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M7 8h10a4 4 0 013.8 5.2l-1 3a2 2 0 01-3.2 1L14 15h-4l-2.6 2.2a2 2 0 01-3.2-1l-1-3A4 4 0 017 8z'/><path d='M7 11v4M5 13h4M16 12h.01M18 14h.01'/></svg>";
    private static final String ICON_FOLDER =
            "<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M3.5 6.5h6l2 2h9v10h-17z'/></svg>";
    private static final String ICON_DOCUMENT =
            "<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M6 3.5h8l4 4v13H6z'/><path d='M14 3.5v4h4M9 12h6M9 16h6'/></svg>";
    private static final String ICON_SAVE_STATE =
            "<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M5 3.5h12l2 2v15H5z'/><path d='M8 3.5v6h8v-6M8 20.5v-7h8v7'/></svg>";
    private static final String ICON_CHEAT_FILE =
            "<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M6 3.5h8l4 4v13H6z'/><path d='M14 3.5v4h4M10 11l-2 2 2 2M14 11l2 2-2 2'/></svg>";

    private static final class Root {
        String id;
        String name;
        File localDirectory;
        Uri treeUri;
        boolean available;
        boolean writable;

        boolean isLocal() {
            return localDirectory != null;
        }
    }

    private static final class Entry {
        String name;
        boolean directory;
        long size;
        long modified;
    }

    private static final class DocumentNode {
        Uri uri;
        String name;
        String mimeType;
        long size;
        long modified;

        boolean isDirectory() {
            return DIRECTORY_MIME_TYPE.equals(mimeType);
        }
    }

    private static final class Request {
        String method;
        String path;
        Map<String, String> query;
        Map<String, String> headers;
    }

    private static final class ResponseOutputStream extends FilterOutputStream {
        private long bytesWritten;

        ResponseOutputStream(OutputStream output) {
            super(output);
        }

        long getBytesWritten() {
            return bytesWritten;
        }

        @Override
        public void write(int value) throws IOException {
            out.write(value);
            bytesWritten++;
        }

        @Override
        public void write(byte[] data, int offset, int length) throws IOException {
            out.write(data, offset, length);
            bytesWritten += length;
        }
    }

    private final Context context;
    private final ContentResolver resolver;
    private final boolean chinese;
    private final ExecutorService clients = Executors.newFixedThreadPool(4,
            runnable -> new Thread(runnable, "DingooPieFileClient"));
    private volatile boolean running;
    private ServerSocket serverSocket;
    private Thread acceptThread;
    private String token;

    LanFileManagerServer(Context context, boolean chinese) {
        this.context = context.getApplicationContext();
        resolver = context.getContentResolver();
        this.chinese = chinese;
    }

    synchronized void start() throws IOException {
        if (running) {
            return;
        }
        token = createToken();
        try {
            serverSocket = new ServerSocket(PREFERRED_PORT);
        } catch (IOException exception) {
            serverSocket = new ServerSocket(0);
        }
        running = true;
        acceptThread = new Thread(this::acceptLoop, "DingooPieFileServer");
        acceptThread.start();
        Log.i(TAG, "File manager started port=" + getPort());
    }

    synchronized boolean isRunning() {
        return running && serverSocket != null && !serverSocket.isClosed();
    }

    synchronized int getPort() {
        return serverSocket == null ? 0 : serverSocket.getLocalPort();
    }

    synchronized List<String> getAccessUrls() {
        if (!isRunning()) {
            return Collections.emptyList();
        }
        List<String> urls = new ArrayList<>();
        for (String address : localIpv4Addresses()) {
            urls.add("http://" + address + ":" + getPort() + "/" + token + "/");
        }
        return urls;
    }

    private void acceptLoop() {
        while (running) {
            try {
                Socket socket = serverSocket.accept();
                socket.setSoTimeout(30000);
                CloseableSocketTask task = new CloseableSocketTask(socket, this::handleClient);
                try {
                    clients.execute(task);
                } catch (RuntimeException exception) {
                    try {
                        task.close();
                    } catch (IOException closeException) {
                        Log.w(TAG, "Unable to close rejected file manager client", closeException);
                    }
                    throw exception;
                }
            } catch (IOException exception) {
                if (running) {
                    Log.e(TAG, "File manager accept failed", exception);
                }
            } catch (RuntimeException exception) {
                Log.e(TAG, "File manager client scheduling failed", exception);
            }
        }
    }

    private void handleClient(Socket socket) {
        try (Socket client = socket;
             BufferedInputStream input = new BufferedInputStream(client.getInputStream());
             ResponseOutputStream output = new ResponseOutputStream(
                     new BufferedOutputStream(client.getOutputStream()))) {
            try {
                Request request = readRequest(input);
                if (request != null) {
                    dispatch(request, input, output);
                }
            } catch (IOException | RuntimeException exception) {
                Log.e(TAG, "File manager request failed", exception);
                if (output.getBytesWritten() == 0) {
                    sendText(output, 500,
                            exception.getMessage() == null ? exception.toString() :
                                    exception.getMessage(),
                            "text/plain; charset=utf-8");
                }
            }
            output.flush();
        } catch (IOException exception) {
            Log.w(TAG, "File manager client ended with an I/O error", exception);
        } catch (RuntimeException exception) {
            Log.e(TAG, "File manager request failed", exception);
        }
    }

    private Request readRequest(InputStream input) throws IOException {
        String requestLine = readLine(input);
        if (requestLine == null || requestLine.isEmpty()) {
            return null;
        }
        String[] parts = requestLine.split(" ", 3);
        if (parts.length != 3) {
            throw new IOException("Invalid HTTP request line");
        }
        Request request = new Request();
        request.method = parts[0].toUpperCase(Locale.ROOT);
        String target = parts[1];
        int querySeparator = target.indexOf('?');
        request.path = decode(querySeparator >= 0 ?
                target.substring(0, querySeparator) : target);
        request.query = parseQuery(querySeparator >= 0 ?
                target.substring(querySeparator + 1) : "");
        request.headers = new HashMap<>();
        int headerBytes = requestLine.length();
        while (true) {
            String line = readLine(input);
            if (line == null || line.isEmpty()) {
                break;
            }
            headerBytes += line.length();
            if (headerBytes > MAX_HEADER_BYTES) {
                throw new IOException("HTTP headers are too large");
            }
            int separator = line.indexOf(':');
            if (separator > 0) {
                request.headers.put(line.substring(0, separator).trim()
                                .toLowerCase(Locale.ROOT),
                        line.substring(separator + 1).trim());
            }
        }
        return request;
    }

    private static String readLine(InputStream input) throws IOException {
        ByteArrayOutputStream buffer = new ByteArrayOutputStream();
        int previous = -1;
        while (buffer.size() <= MAX_HEADER_BYTES) {
            int value = input.read();
            if (value < 0) {
                break;
            }
            if (previous == '\r' && value == '\n') {
                byte[] bytes = buffer.toByteArray();
                return new String(bytes, 0, Math.max(0, bytes.length - 1),
                        StandardCharsets.ISO_8859_1);
            }
            buffer.write(value);
            previous = value;
        }
        if (buffer.size() == 0) {
            return null;
        }
        throw new IOException("Invalid HTTP line ending");
    }

    private void dispatch(Request request, InputStream input, OutputStream output)
            throws IOException {
        String prefix = "/" + token;
        if (!request.path.equals(prefix) && !request.path.startsWith(prefix + "/")) {
            sendText(output, 404, "Not Found", "text/plain; charset=utf-8");
            return;
        }
        String route = request.path.substring(prefix.length());
        if (route.isEmpty() || "/".equals(route)) {
            sendHtml(output, rootPage());
        } else if ("/browse".equals(route) && "GET".equals(request.method)) {
            sendHtml(output, browsePage(request.query));
        } else if ("/download".equals(route) && "GET".equals(request.method)) {
            sendDownload(output, request.query);
        } else if ("/upload".equals(route) && "POST".equals(request.method)) {
            upload(request, input);
            sendText(output, 200, "OK", "text/plain; charset=utf-8");
        } else if ("/rename".equals(route) && "POST".equals(request.method)) {
            rename(request.query);
            sendText(output, 200, "OK", "text/plain; charset=utf-8");
        } else if ("/delete".equals(route) && "POST".equals(request.method)) {
            delete(request.query);
            sendText(output, 200, "OK", "text/plain; charset=utf-8");
        } else if ("/remove-root".equals(route) && "POST".equals(request.method)) {
            removeAuthorizedRoot(request.query);
            sendRedirect(output, "/" + token + "/");
        } else {
            sendText(output, 404, "Not Found", "text/plain; charset=utf-8");
        }
    }

    private String rootPage() throws IOException {
        StringBuilder body = pageHeader(text("\u6587\u4ef6\u7ba1\u7406", "File Manager"));
        body.append("<div class=page-heading><h1>")
                .append(escapeHtml(text("\u6587\u4ef6\u7ba1\u7406", "File Manager")))
                .append("</h1>").append(themeButton()).append("</div><p>")
                .append(escapeHtml(text("\u9009\u62e9\u8981\u8bbf\u95ee\u7684\u76ee\u5f55\u3002",
                        "Choose a directory to manage."))).append("</p><div class=roots>");
        for (Root root : roots()) {
            appendRoot(body, root);
        }
        return pageFooter(body.append("</div>"));
    }

    private void appendRoot(StringBuilder body, Root root) {
        body.append("<div class='root-row")
                .append(root.available ? "" : " unavailable")
                .append("'>");
        if (root.available) {
            body.append("<a class=root href=\"").append(route("browse", root.id, ""))
                    .append("\">");
        } else {
            body.append("<div class=root aria-disabled=true>");
        }
        body.append("<strong>").append(escapeHtml(root.name)).append("</strong><span>")
                .append(escapeHtml(root.available ?
                        (root.writable ? text("\u53ef\u8bfb\u5199", "Read and write") :
                                text("\u53ea\u8bfb", "Read only")) :
                        text("\u76ee\u5f55\u4e0d\u53ef\u7528", "Folder unavailable")))
                .append(root.available ? "</span></a>" : "</span></div>");
        if (root.treeUri != null) {
            String removeLabel = escapeHtml(text("\u79fb\u9664\u6388\u6743\u76ee\u5f55",
                    "Remove authorized folder"));
            String confirmation = escapeHtml(text(
                    "\u4ec5\u79fb\u9664\u6a21\u62df\u5668\u5bf9\u8be5\u76ee\u5f55\u7684\u6388\u6743\uff0c\u4e0d\u4f1a\u5220\u9664\u76ee\u5f55\u6216\u6587\u4ef6\u3002\u786e\u5b9a\u7ee7\u7eed\uff1f",
                    "This only removes emulator access and does not delete the folder or its files. Continue?"));
            body.append("<form method=post action=\"")
                    .append(route("remove-root", root.id, ""))
                    .append("\" onsubmit=\"return confirm('").append(confirmation)
                    .append("')\"><button class='danger icon-button' title=\"")
                    .append(removeLabel).append("\" aria-label=\"")
                    .append(removeLabel).append("\">").append(ICON_DELETE)
                    .append("</button></form>");
        }
        body.append("</div>");
    }

    private String browsePage(Map<String, String> query) throws IOException {
        Root root = requireRoot(query.get("root"));
        String path = normalizePath(query.get("path"));
        List<Entry> entries = list(root, path);
        StringBuilder body = pageHeader(root.name);
        body.append(browseToolbar(root.name, path));
        if (!path.isEmpty()) {
            body.append(parentDirectoryLink(root.id, path));
        }
        if (root.writable) {
            body.append(uploadSection());
        }
        String downloadLabel = escapeHtml(text("\u4e0b\u8f7d", "Download"));
        String renameLabel = escapeHtml(text("\u91cd\u547d\u540d", "Rename"));
        String deleteLabel = escapeHtml(text("\u5220\u9664", "Delete"));
        body.append(fileTableHeader());
        for (Entry entry : entries) {
            body.append(fileRow(root, path, entry, downloadLabel, renameLabel, deleteLabel));
        }
        body.append("</tbody></table></div>")
                .append(pageScript(root.id, path));
        return pageFooter(body);
    }

    private String browseToolbar(String rootName, String path) {
        return new StringBuilder("<div class=toolbar><a href=\"/")
                .append(token).append("/\">")
                .append(escapeHtml(text("\u76ee\u5f55", "Roots"))).append("</a><strong>")
                .append(escapeHtml(rootName)).append(" / ").append(escapeHtml(path))
                .append("</strong>").append(themeButton()).append("</div>")
                .toString();
    }

    private String parentDirectoryLink(String rootId, String path) {
        return new StringBuilder("<p><a href=\"")
                .append(route("browse", rootId, parentPath(path)))
                .append("\">&#8592; ")
                .append(escapeHtml(text("\u4e0a\u7ea7\u76ee\u5f55", "Parent directory")))
                .append("</a></p>")
                .toString();
    }

    private String uploadSection() {
        String selectFilesLabel = escapeHtml(text("\u9009\u62e9\u591a\u4e2a\u6587\u4ef6", "Choose Multiple Files"));
        String noFilesLabel = escapeHtml(text("\u672a\u9009\u62e9\u6587\u4ef6", "No files selected"));
        String uploadLabel = escapeHtml(text("\u4e0a\u4f20", "Upload"));
        return new StringBuilder("<section class=file-tools><div class='tool-group upload-group'><input class=file-input id=files type=file multiple><label class='icon-button file-select-button' for=files title=\"")
                .append(selectFilesLabel).append("\" aria-label=\"")
                .append(selectFilesLabel).append("\">").append(ICON_FILE_SELECT)
                .append("</label><span class=file-selection id=fileSelection>")
                .append(noFilesLabel).append("</span><button class='tool-action icon-button' id=uploadButton onclick=uploadFiles() title=\"")
                .append(uploadLabel).append("\" aria-label=\"")
                .append(uploadLabel).append("\">").append(ICON_UPLOAD)
                .append("</button></div><div class=selection-queue id=selectionQueue hidden></div><span id=status></span></section>")
                .toString();
    }

    private String fileTableHeader() {
        return new StringBuilder("<div class=table-wrap><table><thead><tr><th>")
                .append(escapeHtml(text("\u540d\u79f0", "Name"))).append("</th><th>")
                .append(escapeHtml(text("\u5927\u5c0f", "Size"))).append("</th><th>")
                .append(escapeHtml(text("\u64cd\u4f5c", "Actions")))
                .append("</th></tr></thead><tbody>")
                .toString();
    }

    private String fileRow(Root root, String path, Entry entry,
            String downloadLabel, String renameLabel, String deleteLabel) {
        String childPath = joinPath(path, entry.name);
        StringBuilder row = new StringBuilder("<tr><td>");
        if (entry.directory) {
            row.append(fileIcon(entry.name, true)).append(" <a href=\"")
                    .append(route("browse", root.id, childPath))
                    .append("\">").append(escapeHtml(entry.name)).append("</a>");
        } else {
            row.append(fileIcon(entry.name, false)).append(" ").append(escapeHtml(entry.name));
        }
        row.append("</td><td>").append(entry.directory ? "" : formatSize(entry.size))
                .append("</td><td>");
        if (!entry.directory) {
            row.append(actionLink(route("download", root.id, childPath), downloadLabel, ICON_DOWNLOAD));
        }
        if (root.writable) {
            row.append(actionButton("renameEntry('" + js(childPath) + "','" + js(entry.name) + "')", renameLabel, ICON_RENAME))
                    .append(actionButton("deleteEntry('" + js(childPath) + "')", deleteLabel, ICON_DELETE, true));
        }
        return row.append("</td></tr>").toString();
    }

    private String actionLink(String href, String label, String icon) {
        return new StringBuilder("<a class='action icon-button' href=\"")
                .append(href).append("\" title=\"")
                .append(label).append("\" aria-label=\"")
                .append(label).append("\">").append(icon).append("</a> ")
                .toString();
    }

    private String actionButton(String onclick, String label, String icon) {
        return actionButton(onclick, label, icon, false);
    }

    private String actionButton(String onclick, String label, String icon, boolean danger) {
        String className = danger ? "danger icon-button" : "icon-button";
        return new StringBuilder("<button class='").append(className).append("' onclick=")
                .append(onclick).append(" title=\"")
                .append(label).append("\" aria-label=\"")
                .append(label).append("\">").append(icon).append("</button> ")
                .toString();
    }

    private StringBuilder pageHeader(String title) {
        return new StringBuilder()
                .append("<!doctype html><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\">")
                .append("<title>").append(escapeHtml(title)).append("</title><style>")
                .append(":root{color-scheme:dark;--page:#111820;--text:#edf3f8;--panel:#17222d;--border:#40515f;--line:#31404d;--muted:#aebdca;--subtle:#71808d;--link:#78c8ff;--control:#263945;--control-hover:#314b59;--control-border:#587485;--control-text:#edf4f7;--input:#111820;--picker:#22343e;--danger:#493039;--danger-hover:#5c3843;--danger-border:#986270;--danger-text:#ffdbe1;--scroll-thumb:rgba(225,235,242,.34);--scroll-thumb-hover:rgba(225,235,242,.52);--scroll-thumb-border:rgba(255,255,255,.16);--copyright:#9cabb7}")
                .append(":root[data-theme=light]{color-scheme:light;--page:#f4f6f8;--text:#17222d;--panel:#fff;--border:#c7d0d8;--line:#dce2e7;--muted:#52616e;--subtle:#71808d;--link:#176b9c;--control:#f4f7f9;--control-hover:#e8eff3;--control-border:#9db2be;--control-text:#24414f;--input:#fff;--picker:#eef3f6;--danger:#fff2f4;--danger-hover:#ffe5e9;--danger-border:#d98b96;--danger-text:#9b2f3e;--scroll-thumb:rgba(56,75,88,.28);--scroll-thumb-hover:rgba(56,75,88,.42);--scroll-thumb-border:rgba(56,75,88,.14);--copyright:#a7b2bb}")
                .append("@media(prefers-color-scheme:light){:root:not([data-theme]){color-scheme:light;--page:#f4f6f8;--text:#17222d;--panel:#fff;--border:#c7d0d8;--line:#dce2e7;--muted:#52616e;--subtle:#71808d;--link:#176b9c;--control:#f4f7f9;--control-hover:#e8eff3;--control-border:#9db2be;--control-text:#24414f;--input:#fff;--picker:#eef3f6;--danger:#fff2f4;--danger-hover:#ffe5e9;--danger-border:#d98b96;--danger-text:#9b2f3e;--scroll-thumb:rgba(56,75,88,.28);--scroll-thumb-hover:rgba(56,75,88,.42);--scroll-thumb-border:rgba(56,75,88,.14);--copyright:#a7b2bb}}")
                .append("*{box-sizing:border-box}body{font:16px system-ui;background:var(--page);color:var(--text);width:100%;max-width:1100px;margin:0 auto;padding:clamp(12px,3vw,24px)}")
                .append("a{color:var(--link);text-decoration:none}button,input,.action{font:inherit;padding:9px;margin:4px;border-radius:6px;border:1px solid var(--border)}")
                .append("input{background:var(--input);color:var(--text)}button,.action{display:inline-block;background:var(--control);color:var(--control-text);border-color:var(--control-border);font-weight:500;cursor:pointer;box-shadow:0 1px 2px rgba(0,0,0,.12);transition:background .15s ease,border-color .15s ease}button:hover,.action:hover{background:var(--control-hover)}button:disabled{opacity:.55;cursor:wait}.danger{background:var(--danger);color:var(--danger-text);border-color:var(--danger-border)}.danger:hover{background:var(--danger-hover)}.toolbar{display:grid;grid-template-columns:max-content minmax(0,1fr) max-content;gap:14px;align-items:baseline;margin-bottom:16px}.toolbar>a{white-space:nowrap}.toolbar strong{min-width:0;line-height:1.5;overflow-wrap:anywhere}.page-heading{display:grid;grid-template-columns:minmax(0,1fr) max-content;gap:14px;align-items:baseline}.page-heading h1{margin-top:0}")
                .append(".icon-button{display:inline-flex;align-items:center;justify-content:center;width:38px;height:38px;padding:0;vertical-align:middle}.icon-button svg{width:20px;height:20px;fill:none;stroke:currentColor;stroke-width:1.8;stroke-linecap:round;stroke-linejoin:round}.file-tools{display:block;width:100%;padding:14px;margin-bottom:16px;background:var(--panel);border:1px solid var(--border);border-radius:10px}.tool-group{display:grid;grid-template-columns:42px minmax(0,1fr) 42px;gap:10px;align-items:center;min-width:0}.tool-action{width:100%;min-width:0;height:42px;margin:0;padding:0}.file-input{display:none}.file-select-button{width:42px;height:42px;margin:0;background:var(--control);color:var(--control-text);border:1px solid var(--control-border);border-radius:6px;box-shadow:0 1px 2px rgba(0,0,0,.12);cursor:pointer}.file-select-button:hover{background:var(--control-hover)}.file-input:disabled+.file-select-button{opacity:.55;cursor:wait}.file-selection{min-width:0;color:var(--muted);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.selection-queue{display:grid;gap:6px;margin-top:12px;padding-top:12px;border-top:1px solid var(--line)}.queue-item{display:grid;grid-template-columns:minmax(0,1fr) max-content;gap:10px;align-items:center;min-width:0;padding:5px 6px 5px 10px;background:var(--picker);border-radius:6px}.queue-name{min-width:0;overflow-wrap:anywhere}.queue-remove{width:30px;height:30px;margin:0;padding:0;border:0;background:transparent;box-shadow:none;color:var(--muted);font-size:22px;line-height:1}.queue-remove:hover{background:var(--danger);color:var(--danger-text)}.queue-remove:disabled{background:transparent}#status{display:block;margin-top:10px;overflow-wrap:anywhere}")
                .append(".table-wrap{width:100%;overflow-x:auto;overflow-y:hidden;background:var(--panel);border:1px solid var(--border);border-radius:8px;scrollbar-width:thin;scrollbar-color:var(--scroll-thumb) transparent}.table-wrap::-webkit-scrollbar{height:7px}.table-wrap::-webkit-scrollbar-track,.table-wrap::-webkit-scrollbar-corner{background:transparent}.table-wrap::-webkit-scrollbar-thumb{background:var(--scroll-thumb);border:1px solid var(--scroll-thumb-border);border-radius:2px;background-clip:padding-box}.table-wrap::-webkit-scrollbar-thumb:hover{background:var(--scroll-thumb-hover)}table{width:100%;min-width:560px;border-collapse:collapse;background:var(--panel)}th,td{text-align:left;padding:10px;border-bottom:1px solid var(--line)}tbody tr:last-child td{border-bottom:0}td:first-child{overflow-wrap:anywhere}.entry-file-icon{display:inline-grid;grid-template-rows:20px auto;justify-items:center;width:30px;margin-right:3px;vertical-align:middle}.entry-file-icon svg{width:20px;height:20px;fill:none;stroke:currentColor;stroke-width:1.8;stroke-linecap:round;stroke-linejoin:round}.entry-file-icon small{font-size:8px;line-height:1;font-weight:700;letter-spacing:.2px}.folder-entry{color:#d7aa52}.document-entry{color:var(--muted)}.save-state-entry{color:#55b978}.cheat-entry{color:#a879e6}.app-game{color:#52a8e8}.cc-game{color:#e86b6b}th:nth-child(2),td:nth-child(2){text-align:right;white-space:nowrap}th:last-child,td:last-child{width:1%;text-align:right;white-space:nowrap}")
                .append(".roots{display:grid;gap:12px}.root-row{display:grid;grid-template-columns:minmax(0,1fr) max-content;gap:8px;align-items:center;background:var(--panel);padding:8px 8px 8px 16px;border:1px solid var(--border);border-radius:8px}.root-row form{display:flex;margin:0}.root{display:flex;justify-content:space-between;gap:12px;min-width:0;padding:8px 0}.root span{color:var(--muted);white-space:nowrap}.root-row.unavailable{border-style:dashed}.root-row.unavailable .root{color:var(--muted)}")
                .append("footer{margin-top:28px;color:var(--muted);font-size:14px;line-height:1.5;text-align:center}footer p{margin:0}.copyright{color:var(--copyright);white-space:nowrap}.theme-toggle,.theme-toggle:hover{width:auto;height:auto;padding:0;margin:0;background:transparent;border:0;border-radius:0;box-shadow:none;color:var(--link);font-weight:500;white-space:nowrap}.theme-toggle:hover{text-decoration:underline}")
                .append("@media(max-width:520px){body{padding:12px}.toolbar{gap:8px}.root{gap:8px;flex-direction:column}.root span{white-space:normal}}")
                .append("</style><script>(function(){try{const t=localStorage.getItem('dingoopie-theme');if(t)document.documentElement.dataset.theme=t}catch(e){}})();")
                .append("function toggleTheme(){const e=document.documentElement,c=e.dataset.theme,l=matchMedia('(prefers-color-scheme:light)').matches,n=c?c==='light'?'dark':'light':l?'dark':'light';e.dataset.theme=n;try{localStorage.setItem('dingoopie-theme',n)}catch(x){}}</script>")
                ;
    }

    private String themeButton() {
        return new StringBuilder("<button class=theme-toggle onclick=toggleTheme() title=\"")
                .append(escapeHtml(text("\u5207\u6362\u6d45\u8272\u6216\u6df1\u8272\u4e3b\u9898", "Switch light or dark theme")))
                .append("\">").append(escapeHtml(text("\u5207\u6362\u4e3b\u9898", "Switch Theme")))
                .append("</button>").toString();
    }

    private String pageFooter(StringBuilder body) {
        return body.append("<footer><p class=copyright>DingooPie</p></footer>")
                .toString();
    }

    private String pageScript(String rootId, String path) {
        String base = "/" + token;
        return "<script>const root='" + js(rootId) + "',path='" + js(path) +
                "',base='" + js(base) + "',noFiles='" +
                js(text("\u672a\u9009\u62e9\u6587\u4ef6", "No files selected")) +
                "',uploading='" + js(text("\u6b63\u5728\u4e0a\u4f20", "Uploading")) +
                "',queued='" + js(text("\u4e2a\u6587\u4ef6\uff0c\u53ef\u7ee7\u7eed\u6dfb\u52a0", "files selected; choose again to add more")) +
                "',removeFile='" + js(text("\u79fb\u9664\u6587\u4ef6", "Remove file")) +
                "';const q=v=>encodeURIComponent(v),fileInput=document.getElementById('files'),fileLabel=document.getElementById('fileSelection'),uploadButton=document.getElementById('uploadButton'),statusLabel=document.getElementById('status'),selectionQueue=document.getElementById('selectionQueue'),selectedFiles=[];let uploadInProgress=false;const fileKey=f=>f.name+'\\n'+f.size+'\\n'+(f.lastModified||0);function updateFileSelection(){if(fileLabel)fileLabel.textContent=selectedFiles.length?selectedFiles.length+' '+queued:noFiles;if(!selectionQueue)return;while(selectionQueue.firstChild)selectionQueue.removeChild(selectionQueue.firstChild);selectionQueue.hidden=!selectedFiles.length;selectedFiles.forEach((file,index)=>{const item=document.createElement('div'),name=document.createElement('span'),remove=document.createElement('button');item.className='queue-item';name.className='queue-name';name.textContent=file.name;remove.type='button';remove.className='queue-remove';remove.textContent='\\u00d7';remove.title=removeFile;remove.setAttribute('aria-label',removeFile+': '+file.name);remove.disabled=uploadInProgress;remove.addEventListener('click',()=>{if(uploadInProgress)return;selectedFiles.splice(index,1);updateFileSelection()});item.appendChild(name);item.appendChild(remove);selectionQueue.appendChild(item)})}if(fileInput&&fileLabel)fileInput.addEventListener('change',()=>{const keys=new Set(selectedFiles.map(fileKey));for(const file of Array.from(fileInput.files)){const key=fileKey(file);if(!keys.has(key)){selectedFiles.push(file);keys.add(key)}}fileInput.value='';updateFileSelection()});" +
                "async function call(r,a,b){const u=base+'/'+r+'?root='+q(root)+'&path='+q(a.path??path)+(a.name?'&name='+q(a.name):'')+(a.newName?'&newName='+q(a.newName):'');const x=await fetch(u,{method:'POST',body:b});if(!x.ok)throw new Error(await x.text());}" +
                "async function uploadFiles(){if(!selectedFiles.length){alert(noFiles);return}const selected=selectedFiles.slice();uploadInProgress=true;if(uploadButton)uploadButton.disabled=true;if(fileInput)fileInput.disabled=true;updateFileSelection();try{for(let i=0;i<selected.length;i++){const f=selected[i];if(statusLabel)statusLabel.textContent=uploading+' '+(i+1)+'/'+selected.length+': '+f.name;await call('upload',{name:f.name},f)}location.reload()}catch(e){if(statusLabel)statusLabel.textContent=e.message;alert(e.message)}finally{uploadInProgress=false;if(uploadButton)uploadButton.disabled=false;if(fileInput)fileInput.disabled=false;updateFileSelection()}}" +
                "async function renameEntry(p,n){const v=prompt('" +
                js(text("\u65b0\u540d\u79f0", "New name")) +
                "',n);if(v===null||v===n)return;const name=v.trim();if(!name||name===n)return;try{await call('rename',{path:p,newName:name});location.reload()}catch(e){alert(e.message)}}" +
                "async function deleteEntry(p){if(confirm('" +
                js(text("\u786e\u5b9a\u5220\u9664\uff1f", "Delete this item?")) +
                "'))try{await call('delete',{path:p});location.reload()}catch(e){alert(e.message)}}</script>";
    }

    private void sendDownload(OutputStream output, Map<String, String> query)
            throws IOException {
        Root root = requireRoot(query.get("root"));
        String path = normalizePath(query.get("path"));
        Entry entry = stat(root, path);
        if (entry.directory) {
            throw new IOException("Directories cannot be downloaded");
        }
        String headers = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n" +
                "Content-Disposition: attachment; filename*=UTF-8''" + encode(entry.name) + "\r\n" +
                (entry.size >= 0 ? "Content-Length: " + entry.size + "\r\n" : "") +
                "Connection: close\r\n\r\n";
        output.write(headers.getBytes(StandardCharsets.ISO_8859_1));
        try (InputStream stream = openInput(root, path)) {
            copy(stream, output, -1);
        }
    }

    private void upload(Request request, InputStream input) throws IOException {
        Root root = requireWritableRoot(request.query.get("root"));
        String directoryPath = normalizePath(request.query.get("path"));
        String name = validateName(request.query.get("name"));
        long length = parseContentLength(request.headers.get("content-length"));
        try (StagedUploadFile staged = StagedUploadFile.receive(
                context.getCacheDir(), input, length);
             InputStream stagedInput = staged.openInput();
             OutputStream stream = openOutput(root, joinPath(directoryPath, name))) {
            copy(stagedInput, stream, length);
        }
    }

    private void rename(Map<String, String> query) throws IOException {
        Root root = requireWritableRoot(query.get("root"));
        String path = normalizePath(query.get("path"));
        if (path.isEmpty()) {
            throw new IOException("The root directory cannot be renamed");
        }
        if (root.isLocal()) {
            File source = resolveLocal(root, path);
            String name = FileNameRules.normalizeRenameTarget(
                    source.getName(), query.get("newName"));
            if (name == null) {
                return;
            }
            File target = new File(source.getParentFile(), name).getCanonicalFile();
            ensureWithin(root.localDirectory, target);
            if (target.exists() || !source.renameTo(target)) {
                throw new IOException("Could not rename item");
            }
            return;
        }
        DocumentNode source = resolveDocument(root, path);
        if (source == null) {
            throw new IOException("Could not rename item");
        }
        String name = FileNameRules.normalizeRenameTarget(
                source.name, query.get("newName"));
        if (name == null) {
            return;
        }
        DocumentNode parent = resolveDocument(root, parentPath(path));
        DocumentNode existing = findDocumentChild(root, parent, name);
        if (existing != null || DocumentsContract.renameDocument(
                resolver, source.uri, name) == null) {
            throw new IOException("Could not rename item");
        }
    }

    private void delete(Map<String, String> query) throws IOException {
        Root root = requireWritableRoot(query.get("root"));
        String path = normalizePath(query.get("path"));
        if (path.isEmpty()) {
            throw new IOException("The root directory cannot be deleted");
        }
        ensureDirectoryEmpty(root, path);
        if (root.isLocal()) {
            if (!resolveLocal(root, path).delete()) {
                throw new IOException("Could not delete item");
            }
            return;
        }
        DocumentNode target = resolveDocument(root, path);
        if (target == null || !DocumentsContract.deleteDocument(resolver, target.uri)) {
            throw new IOException("Could not delete item");
        }
    }

    private void removeAuthorizedRoot(Map<String, String> query) throws IOException {
        Root root = requireRoot(query.get("root"));
        if (root.treeUri == null) {
            throw new IOException(text("\u5185\u90e8\u76ee\u5f55\u6388\u6743\u4e0d\u80fd\u79fb\u9664",
                    "The private directory permission cannot be removed"));
        }
        int permissionFlags = 0;
        for (UriPermission permission : resolver.getPersistedUriPermissions()) {
            if (!root.treeUri.equals(permission.getUri())) {
                continue;
            }
            if (permission.isReadPermission()) {
                permissionFlags |= Intent.FLAG_GRANT_READ_URI_PERMISSION;
            }
            if (permission.isWritePermission()) {
                permissionFlags |= Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
            }
        }
        if (permissionFlags == 0) {
            return;
        }
        try {
            resolver.releasePersistableUriPermission(root.treeUri, permissionFlags);
            Log.i(TAG, "Removed persisted directory permission " + root.treeUri);
        } catch (IllegalArgumentException | SecurityException exception) {
            throw new IOException(text("\u65e0\u6cd5\u79fb\u9664\u6388\u6743\u76ee\u5f55",
                    "Could not remove the authorized folder"), exception);
        }
    }

    private void ensureDirectoryEmpty(Root root, String path) throws IOException {
        Entry entry = stat(root, path);
        if (entry.directory && !list(root, path).isEmpty()) {
            throw new IOException(text("\u975e\u7a7a\u6587\u4ef6\u5939\u4e0d\u80fd\u5220\u9664",
                    "Non-empty folders cannot be deleted"));
        }
    }

    private List<Root> roots() {
        List<Root> result = new ArrayList<>();
        Root privateRoot = new Root();
        privateRoot.id = "private";
        privateRoot.name = text("\u6a21\u62df\u5668\u79c1\u6709\u76ee\u5f55", "Emulator Private Files");
        privateRoot.localDirectory = context.getFilesDir();
        privateRoot.available = true;
        privateRoot.writable = true;
        result.add(privateRoot);

        List<UriPermission> permissions = new ArrayList<>(resolver.getPersistedUriPermissions());
        Collections.sort(permissions, new Comparator<UriPermission>() {
            @Override
            public int compare(UriPermission left, UriPermission right) {
                return left.getUri().toString().compareTo(right.getUri().toString());
            }
        });
        int unnamedIndex = 0;
        for (UriPermission permission : permissions) {
            Uri uri = permission.getUri();
            if (!permission.isReadPermission() || !isTreeUri(uri)) {
                continue;
            }
            Root root = new Root();
            root.id = "tree:" + uri;
            root.treeUri = uri;
            root.writable = permission.isWritePermission();
            try {
                Uri documentUri = rootDocumentUri(uri);
                root.name = queryName(documentUri);
                root.available = isDocumentTreeAvailable(root, documentUri);
            } catch (RuntimeException exception) {
                Log.w(TAG, "Persisted directory is unavailable " + uri, exception);
            }
            if (root.name == null || root.name.isEmpty()) {
                root.name = fallbackTreeName(uri, ++unnamedIndex);
            }
            result.add(root);
        }
        return result;
    }

    private String fallbackTreeName(Uri treeUri, int index) {
        try {
            String documentId = DocumentsContract.getTreeDocumentId(treeUri);
            int separator = Math.max(documentId.lastIndexOf('/'), documentId.lastIndexOf(':'));
            String name = documentId.substring(separator + 1);
            if (!name.isEmpty()) {
                return name;
            }
        } catch (RuntimeException exception) {
            Log.w(TAG, "Unable to derive persisted directory name " + treeUri, exception);
        }
        return text("\u6388\u6743\u76ee\u5f55", "Authorized Folder") + " " + index;
    }

    private boolean isDocumentTreeAvailable(Root root, Uri documentUri) {
        try {
            DocumentNode node = queryNode(root, documentUri);
            return node != null && node.isDirectory();
        } catch (RuntimeException exception) {
            Log.w(TAG, "Unable to access persisted directory " + root.treeUri, exception);
            return false;
        }
    }

    private Root requireRoot(String id) throws IOException {
        for (Root root : roots()) {
            if (root.id.equals(id)) {
                return root;
            }
        }
        throw new IOException("Unknown root directory");
    }

    private Root requireWritableRoot(String id) throws IOException {
        Root root = requireRoot(id);
        if (!root.writable) {
            throw new IOException("This directory is read only");
        }
        return root;
    }

    private List<Entry> list(Root root, String path) throws IOException {
        List<Entry> entries = new ArrayList<>();
        if (root.isLocal()) {
            File directory = resolveLocal(root, path);
            File[] files = directory.listFiles();
            if (!directory.isDirectory() || files == null) {
                throw new IOException("Directory could not be listed");
            }
            for (File file : files) {
                Entry entry = new Entry();
                entry.name = file.getName();
                entry.directory = file.isDirectory();
                entry.size = entry.directory ? 0 : file.length();
                entry.modified = file.lastModified();
                entries.add(entry);
            }
        } else {
            DocumentNode directory = resolveDocument(root, path);
            if (directory == null || !directory.isDirectory()) {
                throw new IOException("Directory could not be listed");
            }
            Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(
                    root.treeUri, DocumentsContract.getDocumentId(directory.uri));
            try (Cursor cursor = resolver.query(children, documentProjection(),
                    null, null, null)) {
                if (cursor == null) {
                    throw new IOException("Directory could not be listed");
                }
                while (cursor.moveToNext()) {
                    DocumentNode node = nodeFromCursor(root, cursor);
                    Entry entry = new Entry();
                    entry.name = node.name;
                    entry.directory = node.isDirectory();
                    entry.size = node.size;
                    entry.modified = node.modified;
                    entries.add(entry);
                }
            }
        }
        Collections.sort(entries, new Comparator<Entry>() {
            @Override
            public int compare(Entry first, Entry second) {
                if (first.directory != second.directory) {
                    return first.directory ? -1 : 1;
                }
                return first.name.compareToIgnoreCase(second.name);
            }
        });
        return entries;
    }

    private Entry stat(Root root, String path) throws IOException {
        Entry entry = new Entry();
        if (root.isLocal()) {
            File file = resolveLocal(root, path);
            if (!file.exists()) {
                throw new IOException("File was not found");
            }
            entry.name = file.getName();
            entry.directory = file.isDirectory();
            entry.size = entry.directory ? 0 : file.length();
            entry.modified = file.lastModified();
            return entry;
        }
        DocumentNode node = resolveDocument(root, path);
        if (node == null) {
            throw new IOException("File was not found");
        }
        entry.name = node.name;
        entry.directory = node.isDirectory();
        entry.size = node.size;
        entry.modified = node.modified;
        return entry;
    }

    private InputStream openInput(Root root, String path) throws IOException {
        if (root.isLocal()) {
            return new FileInputStream(resolveLocal(root, path));
        }
        DocumentNode node = resolveDocument(root, path);
        InputStream stream = node == null ? null : resolver.openInputStream(node.uri);
        if (stream == null) {
            throw new IOException("File could not be opened");
        }
        return stream;
    }

    private OutputStream openOutput(Root root, String path) throws IOException {
        if (root.isLocal()) {
            File file = resolveLocal(root, path);
            File parent = file.getParentFile();
            if (parent == null || !parent.isDirectory()) {
                throw new IOException("Destination directory was not found");
            }
            return new FileOutputStream(file, false);
        }
        DocumentNode parent = resolveDocument(root, parentPath(path));
        String name = fileName(path);
        if (parent == null || !parent.isDirectory()) {
            throw new IOException("Destination directory was not found");
        }
        DocumentNode existing = findDocumentChild(root, parent, name);
        Uri uri = existing == null ? DocumentsContract.createDocument(resolver,
                parent.uri, "application/octet-stream", name) : existing.uri;
        if (existing != null && existing.isDirectory()) {
            throw new IOException("A directory already uses this name");
        }
        OutputStream stream = uri == null ? null : resolver.openOutputStream(uri, "wt");
        if (stream == null) {
            throw new IOException("File could not be created");
        }
        return stream;
    }

    private File resolveLocal(Root root, String path) throws IOException {
        File base = root.localDirectory.getCanonicalFile();
        File target = path.isEmpty() ? base : new File(base, path).getCanonicalFile();
        ensureWithin(base, target);
        return target;
    }

    private static void ensureWithin(File base, File target) throws IOException {
        String basePath = base.getCanonicalPath();
        String targetPath = target.getCanonicalPath();
        if (!targetPath.equals(basePath) &&
                !targetPath.startsWith(basePath + File.separator)) {
            throw new IOException("Path escapes the root directory");
        }
    }

    private DocumentNode resolveDocument(Root root, String path) throws IOException {
        DocumentNode current = queryNode(root, rootDocumentUri(root.treeUri));
        if (current == null || path.isEmpty()) {
            return current;
        }
        for (String segment : path.split("/")) {
            current = findDocumentChild(root, current, segment);
            if (current == null) {
                return null;
            }
        }
        return current;
    }

    private DocumentNode findDocumentChild(Root root, DocumentNode parent, String name)
            throws IOException {
        if (parent == null || !parent.isDirectory()) {
            return null;
        }
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(
                root.treeUri, DocumentsContract.getDocumentId(parent.uri));
        try (Cursor cursor = resolver.query(children, documentProjection(),
                null, null, null)) {
            if (cursor == null) {
                return null;
            }
            while (cursor.moveToNext()) {
                DocumentNode node = nodeFromCursor(root, cursor);
                if (name.equals(node.name)) {
                    return node;
                }
            }
        }
        return null;
    }

    private DocumentNode queryNode(Root root, Uri uri) {
        try (Cursor cursor = resolver.query(uri, documentProjection(), null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                return nodeFromCursor(root, cursor);
            }
        }
        return null;
    }

    private DocumentNode nodeFromCursor(Root root, Cursor cursor) {
        DocumentNode node = new DocumentNode();
        String documentId = cursor.getString(cursor.getColumnIndexOrThrow(
                DocumentsContract.Document.COLUMN_DOCUMENT_ID));
        node.uri = DocumentsContract.buildDocumentUriUsingTree(root.treeUri, documentId);
        node.name = cursor.getString(cursor.getColumnIndexOrThrow(
                DocumentsContract.Document.COLUMN_DISPLAY_NAME));
        node.mimeType = cursor.getString(cursor.getColumnIndexOrThrow(
                DocumentsContract.Document.COLUMN_MIME_TYPE));
        int sizeIndex = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_SIZE);
        node.size = sizeIndex >= 0 && !cursor.isNull(sizeIndex) ? cursor.getLong(sizeIndex) : -1;
        int modifiedIndex = cursor.getColumnIndex(
                DocumentsContract.Document.COLUMN_LAST_MODIFIED);
        node.modified = modifiedIndex >= 0 && !cursor.isNull(modifiedIndex) ?
                cursor.getLong(modifiedIndex) : 0;
        return node;
    }

    private static String[] documentProjection() {
        return new String[]{
                DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE,
                DocumentsContract.Document.COLUMN_SIZE,
                DocumentsContract.Document.COLUMN_LAST_MODIFIED
        };
    }

    private static Uri rootDocumentUri(Uri treeUri) {
        return DocumentsContract.buildDocumentUriUsingTree(
                treeUri, DocumentsContract.getTreeDocumentId(treeUri));
    }

    private static boolean isTreeUri(Uri uri) {
        List<String> segments = uri.getPathSegments();
        return segments.size() >= 2 && "tree".equals(segments.get(0));
    }

    private String queryName(Uri uri) {
        try {
            try (Cursor cursor = resolver.query(uri,
                    new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
                if (cursor != null && cursor.moveToFirst()) {
                    int index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                    return index >= 0 ? cursor.getString(index) : null;
                }
            }
        } catch (RuntimeException exception) {
            Log.w(TAG, "Unable to query persisted directory name " + uri, exception);
        }
        return null;
    }

    private static long parseContentLength(String value) throws IOException {
        if (value == null) {
            throw new IOException("Content-Length is required");
        }
        try {
            long length = Long.parseLong(value);
            if (length < 0 || length > MAX_UPLOAD_BYTES) {
                throw new IOException("Upload size is not allowed");
            }
            return length;
        } catch (NumberFormatException exception) {
            throw new IOException("Invalid Content-Length", exception);
        }
    }

    private static void copy(InputStream input, OutputStream output, long length)
            throws IOException {
        byte[] buffer = new byte[64 * 1024];
        long remaining = length;
        while (remaining != 0) {
            int wanted = remaining < 0 ? buffer.length :
                    (int)Math.min((long)buffer.length, remaining);
            int read = input.read(buffer, 0, wanted);
            if (read < 0) {
                if (remaining > 0) {
                    throw new IOException("Upload ended before Content-Length");
                }
                break;
            }
            output.write(buffer, 0, read);
            if (remaining > 0) {
                remaining -= read;
            }
        }
    }

    private static String normalizePath(String value) throws IOException {
        if (value == null || value.isEmpty()) {
            return "";
        }
        String normalized = value.replace('\\', '/');
        while (normalized.startsWith("/")) {
            normalized = normalized.substring(1);
        }
        while (normalized.endsWith("/")) {
            normalized = normalized.substring(0, normalized.length() - 1);
        }
        if (normalized.isEmpty()) {
            return "";
        }
        for (String segment : normalized.split("/")) {
            validateName(segment);
        }
        return normalized;
    }

    private static String validateName(String name) throws IOException {
        return FileNameRules.normalize(name);
    }

    private static String parentPath(String path) {
        int separator = path.lastIndexOf('/');
        return separator < 0 ? "" : path.substring(0, separator);
    }

    private static String fileName(String path) {
        int separator = path.lastIndexOf('/');
        return separator < 0 ? path : path.substring(separator + 1);
    }

    private static String joinPath(String parent, String name) {
        return parent == null || parent.isEmpty() ? name : parent + "/" + name;
    }

    private static Map<String, String> parseQuery(String query) {
        Map<String, String> values = new HashMap<>();
        if (query == null || query.isEmpty()) {
            return values;
        }
        for (String item : query.split("&")) {
            int separator = item.indexOf('=');
            values.put(decode(separator >= 0 ? item.substring(0, separator) : item),
                    decode(separator >= 0 ? item.substring(separator + 1) : ""));
        }
        return values;
    }

    private static String decode(String value) {
        try {
            return URLDecoder.decode(value, StandardCharsets.UTF_8.name());
        } catch (Exception exception) {
            return "";
        }
    }

    private static String encode(String value) {
        try {
            return URLEncoder.encode(value, StandardCharsets.UTF_8.name())
                    .replace("+", "%20");
        } catch (Exception exception) {
            return "";
        }
    }

    private String route(String route, String root, String path) {
        return "/" + token + "/" + route + "?root=" + encode(root) +
                "&path=" + encode(path);
    }

    private static String escapeHtml(String value) {
        return value == null ? "" : value.replace("&", "&amp;")
                .replace("<", "&lt;").replace(">", "&gt;")
                .replace("\"", "&quot;").replace("'", "&#39;");
    }

    private static String js(String value) {
        return value == null ? "" : value.replace("\\", "\\\\")
                .replace("'", "\\'").replace("\r", "").replace("\n", "\\n")
                .replace("</", "<\\/");
    }

    private String fileIcon(String name, boolean directory) {
        String lowerName = name.toLowerCase(Locale.ROOT);
        String type;
        String className;
        String icon;
        String title;
        if (directory) {
            type = "DIR";
            className = "folder-entry";
            icon = ICON_FOLDER;
            title = text("\u6587\u4ef6\u5939", "Folder");
        } else if (lowerName.endsWith(".app")) {
            type = "APP";
            className = "app-game";
            icon = ICON_GAME_FILE;
            title = type + " " + text("\u6e38\u620f\u6587\u4ef6", "game file");
        } else if (lowerName.endsWith(".cc")) {
            type = "CC";
            className = "cc-game";
            icon = ICON_GAME_FILE;
            title = type + " " + text("\u6e38\u620f\u6587\u4ef6", "game file");
        } else if (lowerName.endsWith(".dps")) {
            type = "DPS";
            className = "save-state-entry";
            icon = ICON_SAVE_STATE;
            title = text("\u5373\u65f6\u5b58\u6863", "Instant save");
        } else if (lowerName.endsWith(".cht")) {
            type = "CHT";
            className = "cheat-entry";
            icon = ICON_CHEAT_FILE;
            title = text("\u91d1\u624b\u6307\u6587\u4ef6", "Cheat file");
        } else {
            type = "FILE";
            className = "document-entry";
            icon = ICON_DOCUMENT;
            title = text("\u6587\u4ef6", "File");
        }
        return "<span class='entry-file-icon " + className + "' title=\"" +
                escapeHtml(title) + "\">" + icon + "<small>" + type +
                "</small></span>";
    }

    private static String formatSize(long bytes) {
        if (bytes < 0) {
            return "";
        }
        if (bytes == 0) {
            return "0 KB";
        }
        if (bytes < 1024) {
            return "< 1 KB";
        }
        double value = bytes;
        String[] units = {"KB", "MB", "GB", "TB"};
        int unit = -1;
        do {
            value /= 1024.0;
            unit++;
        } while (value >= 1024.0 && unit < units.length - 1);
        return String.format(Locale.ROOT, "%.1f %s", value, units[unit]);
    }

    private String text(String chineseText, String englishText) {
        return chinese ? chineseText : englishText;
    }

    private static String createToken() {
        SecureRandom random = new SecureRandom();
        StringBuilder value = new StringBuilder();
        for (int index = 0; index < TOKEN_LENGTH; index++) {
            value.append(TOKEN_ALPHABET.charAt(random.nextInt(TOKEN_ALPHABET.length())));
        }
        return value.toString();
    }

    private static List<String> localIpv4Addresses() {
        List<String> addresses = new ArrayList<>();
        try {
            Enumeration<NetworkInterface> interfaces = NetworkInterface.getNetworkInterfaces();
            while (interfaces != null && interfaces.hasMoreElements()) {
                NetworkInterface network = interfaces.nextElement();
                if (!network.isUp() || network.isLoopback()) {
                    continue;
                }
                Enumeration<InetAddress> items = network.getInetAddresses();
                while (items.hasMoreElements()) {
                    InetAddress address = items.nextElement();
                    String hostAddress = address.getHostAddress();
                    if (address instanceof Inet4Address && !address.isLoopbackAddress() &&
                            address.isSiteLocalAddress() && hostAddress != null) {
                        addresses.add(hostAddress);
                    }
                }
            }
        } catch (Exception exception) {
            Log.e(TAG, "Unable to enumerate network addresses", exception);
        }
        Collections.sort(addresses, new Comparator<String>() {
            @Override
            public int compare(String left, String right) {
                int priority = addressPriority(left) - addressPriority(right);
                return priority != 0 ? priority : left.compareTo(right);
            }
        });
        return addresses;
    }

    private static int addressPriority(String address) {
        if (address.startsWith("192.168.")) {
            return 0;
        }
        if (address.startsWith("172.")) {
            return 1;
        }
        return 2;
    }

    private static void sendHtml(OutputStream output, String body) throws IOException {
        sendText(output, 200, body, "text/html; charset=utf-8");
    }

    private static void sendRedirect(OutputStream output, String location) throws IOException {
        String headers = "HTTP/1.1 303 See Other\r\nLocation: " + location +
                "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
        output.write(headers.getBytes(StandardCharsets.ISO_8859_1));
    }

    private static void sendText(OutputStream output, int status, String body,
            String contentType) throws IOException {
        byte[] data = body.getBytes(StandardCharsets.UTF_8);
        String reason = status == 200 ? "OK" : status == 404 ? "Not Found" :
                status == 500 ? "Internal Server Error" : "Error";
        String headers = "HTTP/1.1 " + status + " " + reason + "\r\n" +
                "Content-Type: " + contentType + "\r\n" +
                "Content-Length: " + data.length + "\r\n" +
                "Cache-Control: no-store\r\nConnection: close\r\n\r\n";
        output.write(headers.getBytes(StandardCharsets.ISO_8859_1));
        output.write(data);
    }

    @Override
    public synchronized void close() {
        running = false;
        if (serverSocket != null) {
            try {
                serverSocket.close();
            } catch (IOException exception) {
                Log.w(TAG, "Unable to close file manager socket", exception);
            }
            serverSocket = null;
        }
        Thread thread = acceptThread;
        acceptThread = null;
        if (thread != null && thread != Thread.currentThread()) {
            try {
                thread.join(2000);
            } catch (InterruptedException exception) {
                Thread.currentThread().interrupt();
            }
        }
        List<Runnable> pendingClients = clients.shutdownNow();
        int closedPendingClients = 0;
        for (Runnable pendingClient : pendingClients) {
            if (pendingClient instanceof CloseableSocketTask) {
                try {
                    ((CloseableSocketTask)pendingClient).close();
                    closedPendingClients++;
                } catch (IOException exception) {
                    Log.w(TAG, "Unable to close pending file manager client", exception);
                }
            }
        }
        try {
            clients.awaitTermination(2, TimeUnit.SECONDS);
        } catch (InterruptedException exception) {
            Thread.currentThread().interrupt();
        }
        Log.i(TAG, "File manager stopped pending_clients_closed=" + closedPendingClients);
    }
}
