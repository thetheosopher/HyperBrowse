# HyperBrowse File Management Workflow

## 1. Purpose

This document defines the first native file-management pass for HyperBrowse.

The goal is to let users complete common browse-and-cull workflows without leaving the app, while preserving HyperBrowse's startup and rendering priorities.

## 2. Scope

### First implementation pass

- copy selected files to another folder
- move selected files to another folder
- delete selected files to Recycle Bin
- permanently delete selected files
- reveal the primary selected file in Explorer
- open the containing folder for the primary selected file
- copy selected file paths to the clipboard
- show file properties for the primary selected file

### Deferred to a later pass

- rename in place
- batch rename
- recent destinations and favorite destinations
- RAW+JPEG paired actions
- drag-and-drop between panes or instances
- undo surface inside HyperBrowse

## 3. UX Goals

1. Keep file actions close to the current selection.
2. Use native Windows semantics instead of custom conflict or delete behavior.
3. Never block thumbnail paint, scrolling, or viewer interaction on file I/O.
4. Preserve selection and scroll position where practical after file actions complete.
5. Avoid full-folder reloads for simple deletes or moves out of the current folder.

## 4. Command Surface

## 4.1 Main menu

Add under `File`:

- `Copy Selection...`
- `Move Selection...`
- `Delete`
- `Delete Permanently`
- `Reveal in Explorer`
- `Open Containing Folder`
- `Copy Path`
- `Properties`

The commands should sit near `Open` and `Image Information`, above the batch-convert block.

## 4.2 Browser context menu

Add the same selection-oriented commands to the browser context menu.

Recommended order:

1. Open
2. Reveal in Explorer
3. Open Containing Folder
4. Copy Path
5. Properties
6. separator
7. Copy Selection...
8. Move Selection...
9. Delete
10. Delete Permanently
11. separator
12. existing slideshow / batch-convert / JPEG orientation items

## 4.3 Shortcuts

Required first-pass shortcuts:

- `Delete`: delete to Recycle Bin
- `Shift+Delete`: permanent delete
- `Ctrl+Shift+C`: copy selected paths
- `Ctrl+E`: reveal primary selected file in Explorer
- `Alt+Enter`: show properties for the primary selected file

Copy and move do not require default accelerators in the first pass.

## 5. Selection Rules

- All file actions operate on the ordered current selection from the browser pane.
- If there is no selection, the commands are disabled.
- Explorer reveal and Properties act on the primary selected item.
- Copy Path copies all selected file paths separated by CRLF.

## 6. API Choices

## 6.1 Copy, move, delete

Use `IFileOperation`.

Reasons:

- native Windows copy/move/delete semantics
- Recycle Bin support without legacy APIs
- shell conflict handling
- elevation and permission prompts consistent with Explorer
- low dependency cost because it is an inbox COM API

Implementation notes:

- execute `IFileOperation` on a background STA thread
- set the owner window so native shell prompts remain parented correctly
- use a progress sink to capture per-item success/failure and created paths

## 6.2 Reveal in Explorer

Use `SHOpenFolderAndSelectItems` for the primary selected file or same-folder multi-selection.

Fallback:

- if selection spans multiple parent folders, reveal only the primary selected item

## 6.3 Open containing folder

Use `ShellExecuteW(..., "open", folderPath, ...)`.

## 6.4 Copy path

Use the Win32 clipboard APIs with `CF_UNICODETEXT`.

## 6.5 Properties

Use `ShellExecuteW(..., "properties", filePath, ...)`.

## 7. Async Architecture

Introduce a dedicated `FileOperationService` following the existing service pattern used by batch convert and folder watching.

### Responsibilities

- marshal the requested operation into an async worker
- initialize COM in the worker thread
- run the `IFileOperation` request
- collect per-item outcomes through a progress sink
- post a completion message back to `MainWindow`

### Message contract

`FileOperationUpdate` should include:

- request id
- operation type
- requested count
- succeeded source paths
- created paths when relevant
- failed count
- aborted flag
- destination folder when relevant
- summary or error message

## 8. MainWindow Integration

`MainWindow` owns the service and remains the command router.

### Responsibilities

- gather selected paths from `BrowserPane`
- choose destination folders for copy/move using the existing folder picker
- confirm delete and permanent delete before dispatch
- start the async service request
- update menu/status state while a file operation is active
- apply targeted model/cache updates when the operation finishes

### State to track

- active file operation request id
- file-operation-active boolean
- current file operation label for status text

## 9. Browser Model Update Rules

Avoid full reloads for common operations.

### Copy

- if copied files are created inside the current visible scope, upsert the created paths into the browser model
- otherwise leave the current model unchanged

### Move

- remove succeeded source paths from the current model when they leave the current visible scope
- upsert created paths if the destination falls inside the current visible scope

### Delete

- remove succeeded source paths from the current model

### Common post-update behavior

- invalidate thumbnail and metadata caches for affected source/created paths
- refresh the browser pane
- restore selection by file path so surviving items stay selected where practical

## 10. Visible Scope Rules

The current visible scope is defined by:

- current folder path
- recursive browsing state

Helper logic should treat a created path as in scope when:

- non-recursive: parent folder equals the current folder
- recursive: path has the current folder as a normalized prefix

## 11. Confirmation Rules

### Delete to Recycle Bin

Prompt in HyperBrowse before dispatch:

- single item: `Move selected image to the Recycle Bin?`
- multi-item: `Move N selected images to the Recycle Bin?`

### Permanent delete

Prompt in HyperBrowse before dispatch with stronger wording:

- single item: `Permanently delete the selected image? This cannot be undone.`
- multi-item: `Permanently delete N selected images? This cannot be undone.`

Using an app-level confirm prevents duplicate-shell prompts from becoming noisy.

## 12. Status and Feedback

While a file operation is active:

- append a short activity segment to the status bar such as `File: Copying 12 items`
- disable the file-operation commands that would start another overlapping request

On completion:

- clear the active status segment
- show a concise completion summary when the shell did not already handle everything through native UI

## 13. Error Handling

- no selection: command is disabled or shows a short informational message
- folder picker cancel: no-op
- clipboard failure: show a short error message
- shell API creation failure: show a concise error message and log it
- partial success: keep the summary explicit instead of implying all files succeeded

## 14. Performance Constraints

1. No synchronous copy/move/delete work on the UI thread.
2. No full-folder reload after every file action.
3. No thumbnail decode or metadata extraction triggered unnecessarily for out-of-scope destinations.
4. No extra startup work or initialization for file-management features.

## 15. First-Code-Pass Acceptance Criteria

The first pass is successful if:

- users can copy, move, and delete selected images from HyperBrowse
- delete and move update the current browser contents without a full reload for simple cases
- reveal/open-containing-folder/copy-path/properties work from the selection
- the UI remains responsive during long file operations
- no measurable startup regression is introduced

## 16. Follow-up Work

After the first pass lands:

1. add rename with a small native prompt surface
2. add recent and favorite destinations for copy/move
3. add RAW+JPEG paired operations
4. add richer status/progress reporting for long operations