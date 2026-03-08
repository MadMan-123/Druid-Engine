#include "hub.h"
#include "../deps/imgui/imgui.h"
#include "../deps/imgui/imgui_impl_opengl3.h"
#include "../deps/imgui/imgui_impl_sdl3.h"

Application *hubApplication = NULL;
c8 hubProjectDir[MAX_PATH_LENGTH] = "";
b8   hubProjectSelected             = false;

// Config file written next to the editor binary
static const c8 *HUB_CONFIG = "../projects.conf";

// Required subdirs for a valid Druid project
static const c8 *REQUIRED_DIRS[]  = { "src", "res", "bin", "scenes" };
static const u32   REQUIRED_DIR_COUNT = 4;

#define MAX_SAVED_PROJECTS 64
static c8  savedProjects[MAX_SAVED_PROJECTS][MAX_PATH_LENGTH];
static i32 savedCount          = 0;
static i32 selectedProjectIdx  = -1;

static c8  statusMsg[MAX_PATH_LENGTH] = "";
static f32   statusTimer                = 0.0f;

static SDL_Event hubEvnt;


static void setStatus(const c8 *msg)
{
    strncpy(statusMsg, msg, sizeof(statusMsg) - 1);
    statusMsg[sizeof(statusMsg) - 1] = '\0';
    statusTimer = 4.0f;
}

// Returns pointer to the last path component (the project folder name).
static const c8 *pathBasename(const c8 *path)
{
    const c8 *s = strrchr(path, '/');
    const c8 *b = strrchr(path, '\\');
    const c8 *sep = s > b ? s : b;
    return sep ? sep + 1 : path;
}

static b8 isValidProject(const c8 *path)
{
    if (!dirExists(path))
        return false;
    c8 sub[MAX_PATH_LENGTH];
    for (u32 i = 0; i < REQUIRED_DIR_COUNT; i++)
    {
        snprintf(sub, sizeof(sub), "%s/%s", path, REQUIRED_DIRS[i]);
        if (!dirExists(sub))
            return false;
    }
    return true;
}

static b8 scaffoldProject(const c8 *path)
{
    c8 sub[MAX_PATH_LENGTH];
    for (u32 i = 0; i < REQUIRED_DIR_COUNT; i++)
    {
        snprintf(sub, sizeof(sub), "%s/%s", path, REQUIRED_DIRS[i]);
        if (!createDir(sub))
            return false;
    }
    // also create deps
    snprintf(sub, sizeof(sub), "%s/deps", path);
    createDir(sub);
    return true;
}


static void saveConfig()
{
    // build a newline-delimited buffer
    c8 buf[MAX_SAVED_PROJECTS * MAX_PATH_LENGTH];
    u32 offset = 0;
    for (i32 i = 0; i < savedCount; i++)
    {
        u32 len = (u32)strlen((c8 *)savedProjects[i]);
        if (offset + len + 1 >= sizeof(buf))
            break;
        memcpy(buf + offset, savedProjects[i], len);
        offset += len;
        buf[offset++] = '\n';
    }
    writeFile(HUB_CONFIG, (const u8 *)buf , offset);
}

static void loadConfig()
{
    savedCount = 0;
    if (!fileExists(HUB_CONFIG))
        return;

    FileData *fd = loadFile(HUB_CONFIG);
    if (!fd)
        return;

    // parse line by line
    c8 *cursor = (c8 *)fd->data;
    c8 *end    = cursor + fd->size;
    while (cursor < end && savedCount < MAX_SAVED_PROJECTS)
    {
        // find line end
        c8 *nl = cursor;
        while (nl < end && *nl != '\n' && *nl != '\r')
            nl++;

        u32 len = (u32)(nl - cursor);
        if (len > 0 && len < MAX_PATH_LENGTH)
        {
            memcpy(savedProjects[savedCount], cursor, len);
            savedProjects[savedCount][len] = '\0';
            normalizePath(savedProjects[savedCount]);
            savedCount++;
        }
        // skip \r\n
        cursor = nl + 1;
        if (cursor < end && cursor[-1] == '\r' && cursor[0] == '\n')
            cursor++;
    }

    freeFileData(fd);
}

static void addToSaved(const c8 *path)
{
    // remove duplicate if present
    for (i32 i = 0; i < savedCount; i++)
    {
        if (strcmp((c8 *)savedProjects[i], path) == 0)
        {
            // shift left to remove
            for (i32 j = i; j < savedCount - 1; j++)
                memcpy(savedProjects[j], savedProjects[j + 1], MAX_PATH_LENGTH);
            savedCount--;
            break;
        }
    }
    // shift right to insert at front
    if (savedCount < MAX_SAVED_PROJECTS)
    {
        for (i32 i = savedCount; i > 0; i--)
            memcpy(savedProjects[i], savedProjects[i - 1], MAX_PATH_LENGTH);
        strncpy((c8 *)savedProjects[0], path, MAX_PATH_LENGTH - 1);
        savedProjects[0][MAX_PATH_LENGTH - 1] = '\0';
        savedCount++;
    }
    saveConfig();
}

static void removeFromSaved(i32 idx)
{
    if (idx < 0 || idx >= savedCount) return;
    for (i32 i = idx; i < savedCount - 1; i++)
        memcpy(savedProjects[i], savedProjects[i + 1], MAX_PATH_LENGTH);
    savedCount--;
    selectedProjectIdx = -1;
    saveConfig();
}


void hubProcessInput(void *appData)
{
    Application *app = (Application *)appData;
    SDL_PumpEvents();
    while (SDL_PollEvent(&hubEvnt))
    {
        ImGui_ImplSDL3_ProcessEvent(&hubEvnt);
        if (hubEvnt.type == SDL_EVENT_QUIT)
            app->state = EXIT;
    }
}

void hubStart()
{
    loadConfig();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForOpenGL(hubApplication->display->sdlWindow,
                                 hubApplication->display->glContext);
    ImGui_ImplOpenGL3_Init("#version 410");
}

void hubUpdate(f32 dt)
{
    if (statusTimer > 0.0f)
        statusTimer -= dt;

    if (hubProjectSelected)
        hubApplication->state = EXIT;
}

void hubRender(f32 dt)
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(displaySize);
    ImGui::Begin("##hub", nullptr,
                 ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoBringToFrontOnFocus);

    const c8 *title = "DRUID ENGINE";
    f32 titleW = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX((displaySize.x - titleW) * 0.5f);
    ImGui::TextUnformatted(title);
    ImGui::Separator();
    ImGui::Spacing();

    const f32 listW = displaySize.x * 0.6f;
    ImGui::BeginChild("##projectList", ImVec2(listW, displaySize.y - 120.0f), true);

    ImGui::TextDisabled("Recent Projects");
    ImGui::Separator();

    for (i32 i = 0; i < savedCount; i++)
    {
        const c8 *p     = (const c8 *)savedProjects[i];
        b8          valid = isValidProject(p);
        b8        sel   = (i == selectedProjectIdx);

        if (!valid)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));

        c8 label[MAX_PATH_LENGTH + 16];
        snprintf(label, sizeof(label), "%s##%d", pathBasename(p), i);

        if (ImGui::Selectable(label, sel, ImGuiSelectableFlags_AllowDoubleClick))
        {
            selectedProjectIdx = i;
            strncpy(hubProjectDir, p, sizeof(hubProjectDir) - 1);
            hubProjectDir[sizeof(hubProjectDir) - 1] = '\0';
            if (ImGui::IsMouseDoubleClicked(0) && valid)
                hubProjectSelected = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(p);
            if (!valid)
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Missing required directories!");
            ImGui::EndTooltip();
        }

        if (!valid)
            ImGui::PopStyleColor();
    }

    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##actions", ImVec2(0.0f, displaySize.y - 120.0f), false);

    ImGui::TextDisabled("Project Path");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##projectDir", hubProjectDir, sizeof(hubProjectDir));
    ImGui::Spacing();
    ImGui::Spacing();

    const f32 bw = -1.0f;
    const f32 bh = 28.0f;

    // Load existing project
    if (ImGui::Button("Load Project", ImVec2(bw, bh)))
    {
        normalizePath(hubProjectDir);
        if (hubProjectDir[0] == '\0')
            setStatus("Enter a project directory first.");
        else if (!isValidProject(hubProjectDir))
            setStatus("Not a valid Druid project (missing src/res/bin/scenes).");
        else
        {
            addToSaved(hubProjectDir);
            hubProjectSelected = true;
        }
    }

    ImGui::Spacing();

    // Create new project at typed path
    if (ImGui::Button("New Project", ImVec2(bw, bh)))
    {
        normalizePath(hubProjectDir);
        if (hubProjectDir[0] == '\0')
            setStatus("Enter a project directory first.");
        else if (isValidProject(hubProjectDir))
            setStatus("A project already exists there. Use Load Project.");
        else if (scaffoldProject(hubProjectDir))
        {
            addToSaved(hubProjectDir);
            setStatus("Project created.");
            hubProjectSelected = true;
        }
        else
            setStatus("Failed to create directories. Check the path.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::BeginDisabled(selectedProjectIdx < 0);
    if (ImGui::Button("Remove from List", ImVec2(bw, bh)))
        removeFromSaved(selectedProjectIdx);
    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Cancel", ImVec2(bw, bh)))
        hubApplication->state = EXIT;

    ImGui::EndChild();

    ImGui::SetCursorPos(ImVec2(8.0f, displaySize.y - 30.0f));
    ImGui::Separator();
    if (statusTimer > 0.0f)
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", statusMsg);

    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void hubDestroy()
{
    saveConfig();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}