#include "Renderer.h"
#include "Feedback.h"
#include "Core/Solver.h"
#include "imgui.h"
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <deque>
#include <future>
#include <functional>
#include <sstream>
#include <stdexcept>

using namespace Burnside;
namespace
{
constexpr int LeftWidth = 290, RightWidth = 340;
struct Application
{
    HWND Window = nullptr;
    Renderer Graphics;
    Timeline Trail;
    ViewSettings View;
    SceneFrame Scene;
    CompletionEffect Completion;
    int MouseX = 0, MouseY = 0, Wheel = 0;
    unsigned char Buttons = 0;
    unsigned char GuiButtons = 0;
    struct ButtonEvent { unsigned char Buttons; int X, Y; };
    std::deque<ButtonEvent> ButtonEvents;
    bool Pressed = false, Dragged = false, Right = false, Ctrl = false, Minimized = false;
    int DownX = 0, DownY = 0, LastX = 0, LastY = 0;
    int LeftScroll = 0, RightScroll = 0, HistoryHeaderScroll = 0;
    float Duration = 0.22f, RandomCount = 25;
    bool Solving = false, StateSearch = false;
    std::atomic_bool Cancel{false};
    std::future<SolveResult> Future;
    std::string Status = "Ready. Click a sticker to turn a slice.";
    std::string SolverMethod;
    bool HasSolution = false, DemoRewind = false;
    size_t SolutionStart = 0, SolutionEnd = 0;
    std::vector<std::function<void()>> Deferred;
    bool FollowHistory = true;
    size_t PreviousHistory = 0, DrawnHistoryRows = 0;
    std::optional<Move> LastClickMove;
    int LastClickX=0, LastClickY=0;
    bool LastClickControl=false, LastClickRight=false;
    bool Smoke = false, SmokeDone = false;
    bool SmokeControl = false;
    int SmokeStage = 0, SmokeFrames = 0;
    int SmokeClickX = 0, SmokeClickY = 0;
    int StableFrames=0, WarmupFrames=0;
    uint64_t StableHash=0;
    size_t RandomSyncFrames=0;
    bool ResizedSmoke=false, RandomFrameSaved=false;
    Move SmokeHorizontal{}, SmokeVertical{};
    std::wstring Capture;

    ~Application() { Cancel = true; if (Future.valid()) Future.wait(); }
    bool Locked() const { return Trail.Busy() || Solving || DemoRewind; }
    bool OverScene(int x) const { return x > LeftWidth && x < Graphics.Width()-RightWidth; }
    void ChangeOrder(int order)
    {
        Trail = Timeline(order); View = ViewSettings{}; Completion=CompletionEffect{};
        RightScroll = 0; HasSolution = DemoRewind = false; LastClickMove.reset();
        PreviousHistory=0; FollowHistory=true;
        Status = "Ready. Click a row cross; hold Ctrl for a column.";
    }
    void Append(const std::vector<Move>& moves, MoveSource source)
    {
        if (Locked()) return;
        Trail.Append(moves,source); HasSolution = false; DemoRewind = false;
        LastClickMove.reset(); FollowHistory=true;
    }
    void Randomize()
    {
        Append(Scramble(Trail.State().Order(),static_cast<int>(RandomCount),TimeSeed()),MoveSource::Scramble);
        Status = "Randomizing. History records each move as it starts.";
    }
    void StartSolve()
    {
        if (Locked()) return;
        Solving = true; Cancel = false; Status = "Solving in background...";
        LastClickMove.reset();
        const Cube state = Trail.State(); const auto proof = Trail.Certificate(); const bool search = StateSearch;
        Future = std::async(std::launch::async,[this,state,proof,search]()
        {
            SolveOptions options; options.Cancel = &Cancel;
            options.TimeLimit = std::chrono::seconds(search ? 5 : 60);
            return Solve(state,options,search ? nullptr : &proof);
        });
    }
    void PollSolver()
    {
        if (!Solving || Future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        Solving = false;
        try
        {
            auto result = Future.get();
            Status = result.Message; SolverMethod = result.Method;
            if (result.Success() && !result.Moves.empty())
            {
                SolutionStart = Trail.Cursor();
                Trail.Append(result.Moves,MoveSource::Solver,false);
                SolutionEnd = Trail.Entries().size(); HasSolution = true;
                Status = "Solution ready: " + std::to_string(result.Moves.size()) + " moves. Press Demo.";
            }
        }
        catch (const std::exception& e) { Status = e.what(); }
    }
    void Demo()
    {
        if (Solving || !HasSolution) return;
        Trail.Seek(SolutionStart); DemoRewind = true;
        FollowHistory=true; LastClickMove.reset();
        Status = "Demo: animate to solution start, then play the solution.";
    }
    void Tick(float dt)
    {
        PollSolver();
        Trail.Tick(std::min(dt,Duration*0.5f),Duration);
        if (DemoRewind && !Trail.Busy()) { DemoRewind = false; Trail.Seek(SolutionEnd); }
        Completion.Tick(Trail.State().IsSolved(),Trail.Busy(),dt);
    }
    void Click(int x, int y, bool right, bool control)
    {
        if (Solving || DemoRewind || !OverScene(x)) return;
        Move move;
        if (LastClickMove && LastClickRight!=right && LastClickControl==control &&
            std::abs(x-LastClickX)<=5 && std::abs(y-LastClickY)<=5) move=LastClickMove->Inverse();
        else
        {
            const auto hit=BuildScene(Trail,View,Graphics.Width(),Graphics.Height(),LeftWidth,RightWidth,x,y,control);
            if (hit.HoveredSticker<0) return;
            move=ClickMove(Trail.State().Geometry(),hit.HoveredSticker,View,control,!right);
        }
        const auto action=Trail.ApplyManual(move);
        if (action==ManualAction::Ignored) return;
        LastClickMove=move; LastClickX=x; LastClickY=y; LastClickControl=control; LastClickRight=right;
        HasSolution=false; FollowHistory=true;
        Status=(action==ManualAction::Undo ? "Undo: " : action==ManualAction::Redo ? "Redo: " : "Manual: ")+move.ToString(Trail.State().Order());
    }
    void Reset()
    {
        DemoRewind=false; HasSolution=false; LastClickMove.reset(); FollowHistory=true;
        Completion.Reset(true);
        Trail.ResetAnimated(); Status="Rewinding to the solved origin.";
    }
    void Defer(std::function<void()> action) { Deferred.push_back(std::move(action)); }
    void ExecuteDeferred()
    {
        auto actions=std::move(Deferred); Deferred.clear();
        for (auto& action : actions) { try { action(); } catch (const std::exception& e) { Status=e.what(); } }
    }
    void CancelGesture()
    {
        Pressed = false; Dragged = false; Buttons = 0;
        if (ButtonEvents.empty() || ButtonEvents.back().Buttons != 0) ButtonEvents.push_back({0,-1,-1});
    }
    void Export()
    {
        wchar_t executable[MAX_PATH]{}; GetModuleFileNameW(nullptr,executable,MAX_PATH);
        const auto directory = std::filesystem::path(executable).parent_path();
        SaveState(Trail.State(),(directory/"Burnside.state").string());
        SaveMoves({Trail.State().Order(),Trail.Certificate()},(directory/"Burnside.moves").string());
        MoveSequence full; full.Order = Trail.State().Order();
        for (const auto& entry : Trail.Entries()) full.Moves.push_back(entry.Rotation);
        SaveMoves(full,(directory/"Burnside.timeline").string());
        Status = "Saved state, active moves and full timeline beside the executable.";
    }
    void Import()
    {
        wchar_t file[MAX_PATH]{};
        OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = Window;
        dialog.lpstrFilter = L"Burnside moves\0*.moves;*.timeline;*.txt\0All files\0*.*\0\0";
        dialog.lpstrFile = file; dialog.nMaxFile = MAX_PATH;
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&dialog)) return;
        const auto sequence = LoadMoves(std::filesystem::path(file).string());
        ChangeOrder(sequence.Order);
        Trail.Append(sequence.Moves,MoveSource::Manual,false);
        Status = "Imported sequence at origin. Use Play to end or click a row.";
    }
    void Wrapped(const std::string& text, size_t width = 35)
    {
        std::istringstream words(text); std::string word, line;
        while (words >> word)
        {
            if (line.size()+word.size()+1 > width && !line.empty()) { imguiLabel(line.c_str()); line.clear(); }
            if (!line.empty()) line += ' '; line += word;
        }
        if (!line.empty()) imguiLabel(line.c_str());
    }
    void GUI()
    {
        const int width=Graphics.Width(),height=Graphics.Height(),order=Trail.State().Order();
        int guiX=MouseX,guiY=MouseY;
        if (!ButtonEvents.empty())
        {
            const auto event=ButtonEvents.front(); ButtonEvents.pop_front();
            GuiButtons=event.Buttons; guiX=event.X; guiY=event.Y;
        }
        else GuiButtons=Buttons;
        imguiBeginFrame(guiX,height-guiY,GuiButtons,Wheel); Wheel=0;
        DrawFeedback(Scene,Completion,width,height,!Solving && !Pressed);
        imguiBeginScrollArea("BURNSIDE / CUBE",10,10,LeftWidth-20,height-20,&LeftScroll);
        const char* sizes[]={"2 x 2 x 2","3 x 3 x 3","4 x 4 x 4","5 x 5 x 5","6 x 6 x 6","7 x 7 x 7","8 x 8 x 8","9 x 9 x 9","10 x 10 x 10"};
        int selection=order-2;
        if (imguiCombo("Cube size",&selection,sizes,9,!Locked())) Defer([this,selection] { ChangeOrder(selection+2); });
        imguiLabel((std::to_string(Trail.State().Colors().size())+" stickers / 6 faces").c_str());
        imguiLabel(Trail.State().IsSolved() ? "SOLVED" : "SCRAMBLED");
        imguiSeparatorLine();
        if (imguiButton("Solve / prepare sequence",!Locked())) Defer([this] { StartSolve(); });
        if (imguiButton("Cancel solver",Solving)) Defer([this] { Cancel=true; });
        if (imguiButton("Demo solution",HasSolution && !Solving)) Defer([this] { Demo(); });
        if (imguiButton("Reset (animated)",!Solving)) Defer([this] { Reset(); });
        if (imguiButton("Random scramble",!Locked())) Defer([this] { Randomize(); });
        imguiSlider("Random moves",&RandomCount,1,100,1,!Locked());
        imguiSlider("Seconds / turn",&Duration,0.04f,1.0f,0.02f);
        imguiSeparatorLine();
        if (imguiCheck("State-only search (5s limit)",StateSearch,!Locked())) Defer([this] { StateSearch=!StateSearch; });
        if (imguiButton("Export state + timeline",!Locked())) Defer([this] { Export(); });
        if (imguiButton("Load move sequence...",!Locked())) Defer([this] { Import(); });
        imguiSeparatorLine();
        imguiLabel("Drag either button: orbit");
        imguiLabel("Wheel: zoom / scroll panels");
        imguiLabel("Click: row cross");
        imguiLabel("Ctrl + click: column cross");
        imguiLabel("Left / right: opposite directions");
        imguiLabel("Opposite click: undo / redo");
        imguiLabel("Space: pause / resume");
        imguiSeparatorLine();
        Wrapped(Status);
        imguiEndScrollArea();

        const size_t history=Trail.HistoryCount();
        DrawnHistoryRows=history;
        if (history!=PreviousHistory && FollowHistory)
            RightScroll=std::max(0,static_cast<int>((history+1)*24)-(height-229));
        PreviousHistory=history;
        imguiBeginScrollArea("HISTORY",width-RightWidth+10,height-175,RightWidth-20,165,&HistoryHeaderScroll);
        imguiLabel((std::to_string(Trail.Cursor())+" completed"+(Trail.ActiveMove() ? " / turning..." : "")).c_str());
        if (imguiButton(Trail.Busy() ? "Pause at boundary" : "Play / redo to end",!Solving))
            Defer([this] { DemoRewind=false; LastClickMove.reset(); FollowHistory=true;
                if (Trail.Busy()) Trail.StopAtNextBoundary(); else Trail.Seek(Trail.Entries().size()); });
        if (imguiButton("Step back",!Solving && !Trail.Busy() && Trail.Cursor()>0))
            Defer([this] { DemoRewind=false; LastClickMove.reset(); Trail.Seek(Trail.Cursor()-1); });
        if (imguiButton("Step forward",!Solving && !Trail.Busy() && Trail.Cursor()<Trail.Entries().size()))
            Defer([this] { DemoRewind=false; LastClickMove.reset(); Trail.Seek(Trail.Cursor()+1); });
        imguiLabel("Click a recorded move to seek");
        imguiEndScrollArea();

        imguiBeginScrollArea("RECORDED MOVES",width-RightWidth+10,10,RightWidth-20,height-195,&RightScroll);
        if (imguiItem(Trail.Cursor()==0 ? "> 0  Solved origin" : "  0  Solved origin",!Solving))
            Defer([this] { DemoRewind=false; LastClickMove.reset(); Trail.Seek(0); });
        const size_t begin=std::min(history,static_cast<size_t>(std::max(0,RightScroll/24-2)));
        const size_t end=std::min(history,begin+static_cast<size_t>(height/24+8));
        imguiSkipItems(static_cast<int>(begin));
        for (size_t i=begin; i<end; ++i)
        {
            const Entry& entry=Trail.Entries()[i];
            const char source=entry.Source==MoveSource::Manual ? 'M' : entry.Source==MoveSource::Scramble ? 'R' : 'S';
            const bool selected=Trail.ActiveMove() ? i+1==history : i+1==Trail.Cursor();
            const std::string label=(selected ? "> " : "  ")+std::to_string(i+1)+" "+source+"  "+entry.Rotation.ToString(order);
            if (imguiItem(label.c_str(),!Solving)) Defer([this,i] { DemoRewind=false; LastClickMove.reset(); Trail.Seek(i+1); });
        }
        imguiSkipItems(static_cast<int>(history-end));
        imguiEndScrollArea();
        const auto textColor=imguiRGBA(193,207,226);
        const std::string title=std::to_string(order)+" x "+std::to_string(order)+" x "+std::to_string(order)+" RUBIK CUBE";
        imguiDrawText(LeftWidth+24,height-38,IMGUI_ALIGN_LEFT,title.c_str(),textColor);
        imguiDrawText(LeftWidth+24,height-62,IMGUI_ALIGN_LEFT,"Click: row  /  Ctrl: column  /  Drag: orbit",textColor);
        imguiDrawText(LeftWidth+24,45,IMGUI_ALIGN_LEFT,(std::to_string(Scene.RenderedCubies)+" cubies / "+std::to_string(Scene.VisibleStickers)+" stickers").c_str(),textColor);
        if (Scene.HoverMove)
        {
            const bool control=Smoke ? SmokeControl : (GetKeyState(VK_CONTROL)&0x8000)!=0;
            imguiDrawText(LeftWidth+24,22,IMGUI_ALIGN_LEFT,((control ? "Column: " : "Row: ")+Scene.HoverMove->ToString(order)).c_str(),imguiRGBA(255,255,255));
        }
        imguiEndFrame();
    }
    void CaptureSmoke(const std::wstring& suffix)
    {
        if (!Capture.empty() && !Graphics.SaveBMP(Capture+suffix)) throw std::runtime_error("Capture failed");
    }
    void SmokeClick(bool right,bool control)
    {
        const WPARAM modifier=control ? MK_CONTROL : 0;
        SendMessageW(Window,right ? WM_RBUTTONDOWN : WM_LBUTTONDOWN,modifier|(right ? MK_RBUTTON : MK_LBUTTON),MAKELPARAM(SmokeClickX,SmokeClickY));
        SendMessageW(Window,right ? WM_RBUTTONUP : WM_LBUTTONUP,modifier,MAKELPARAM(SmokeClickX,SmokeClickY));
    }
    void PointAtRandomButton()
    {
        const auto* commands=imguiGetRenderQueue();
        for (int i=0; i<imguiGetRenderQueueSize(); ++i)
            if (commands[i].type==IMGUI_GFXCMD_TEXT && commands[i].text.text && std::string(commands[i].text.text)=="Random scramble")
            {
                MouseX=commands[i].text.x+40; MouseY=Graphics.Height()-commands[i].text.y-4;
                return;
            }
        throw std::runtime_error("Random button missing from GUI");
    }
    void PointAtSmokeSticker()
    {
        const int n=Trail.State().Order();
        for (int axis : {2,0,1}) for (int sign : {1,-1})
        {
            Sticker s; s.Axis=static_cast<uint8_t>(axis); s.Sign=static_cast<int8_t>(sign);
            s.Position.fill(static_cast<int8_t>(n/2)); s.Position[axis]=static_cast<int8_t>(sign>0 ? n-1 : 0);
            const auto index=Trail.State().Geometry().Index(s);
            const Vec3 p=Scene.StickerCenters[index];
            const int x=static_cast<int>(std::lround((p.X+1)*Graphics.Width()*0.5f));
            const int y=static_cast<int>(std::lround((1-p.Y)*Graphics.Height()*0.5f));
            const auto hit=BuildScene(Trail,View,Graphics.Width(),Graphics.Height(),LeftWidth,RightWidth,x,y,SmokeControl);
            if (hit.HoveredSticker!=static_cast<int>(index)) continue;
            SmokeClickX=MouseX=x; SmokeClickY=MouseY=y; return;
        }
        throw std::runtime_error("No visible sticker at the orbited camera angle");
    }
    void SmokeTick()
    {
        ++SmokeFrames;
        if (SmokeFrames>10000) throw std::runtime_error("GUI smoke timeout");
        const size_t n=Trail.State().Order();
        if (Scene.RenderedCubies!=n*n*n || Scene.VisibleStickers!=6*n*n) throw std::runtime_error("Wrong NxNxN model geometry");
        if (DrawnHistoryRows!=Trail.HistoryCount()) throw std::runtime_error("GUI/model history mismatch");
        if (Trail.Busy() && !Scene.TurnHints.empty()) throw std::runtime_error("Turn hints must wait for the animation boundary");
        switch (SmokeStage)
        {
        case 0:
            if (Completion.Active()) throw std::runtime_error("Initial solved cube must not celebrate");
            RandomCount=8; Duration=0.055f;
            PointAtRandomButton();
            SendMessageW(Window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(MouseX,MouseY));
            SendMessageW(Window,WM_LBUTTONUP,0,MAKELPARAM(MouseX,MouseY));
            SmokeStage=1; break;
        case 1:
            if (Trail.Entries().empty())
            {
                if (SmokeFrames>12) throw std::runtime_error("Random GUI button did not start the queue");
                break;
            }
            SmokeStage=2;
            [[fallthrough]];
        case 2:
        {
            ++RandomSyncFrames;
            if (Trail.Entries().size()!=8 || Trail.HistoryCount()!=Trail.Cursor()+(Trail.ActiveMove() ? 1 : 0))
                throw std::runtime_error("Random history contains unstarted moves");
            Cube expected(static_cast<int>(n));
            for (size_t i=0; i<Trail.Cursor(); ++i) expected.Apply(Trail.Entries()[i].Rotation);
            if (!(expected==Trail.State())) throw std::runtime_error("Random history and simulated state differ");
            if (Trail.ActiveMove() && !(*Trail.ActiveMove()==Trail.Entries()[Trail.Cursor()].Rotation))
                throw std::runtime_error("Random animation and live history row differ");
            if (!RandomFrameSaved && Trail.ActiveMove() && Trail.Progress()>0.25f)
            { CaptureSmoke(L".random-step.bmp"); RandomFrameSaved=true; }
            if (!Locked()) { StartSolve(); SmokeStage=3; }
            break;
        }
        case 3:
            if (Solving) break;
            if (!HasSolution || DrawnHistoryRows!=8) throw std::runtime_error("Prepared solution leaked into history or solve failed");
            CaptureSmoke(L".scrambled.bmp"); Demo(); SmokeStage=4; break;
        case 4:
            if (Locked()) break;
            if (!Trail.State().IsSolved()) throw std::runtime_error("Demo failed to solve");
            if (!Completion.Active() || Completion.TriggerCount()!=1) throw std::runtime_error("Solving must trigger one celebration");
            if (Completion.Age()<0.65f) break;
            CaptureSmoke(L".celebration.bmp");
            Trail.Seek(3); SmokeStage=5; break;
        case 5:
            if (Locked()) break;
            if (Trail.Cursor()!=3 || DrawnHistoryRows!=3) throw std::runtime_error("Seek did not rewind history");
            Reset(); SmokeStage=6; break;
        case 6:
            if (Locked()) break;
            if (!Trail.State().IsSolved() || DrawnHistoryRows!=0 || !Trail.Entries().empty()) throw std::runtime_error("Reset failed");
            if (Completion.Active()) throw std::runtime_error("Reset must not trigger a completion reward");
            CaptureSmoke(L".solved.bmp"); PointAtSmokeSticker(); SmokeStage=7; break;
        case 7:
            if (!Scene.HoverMove) throw std::runtime_error("No row preview");
            if (Scene.TurnHints.size()!=2 || !(Scene.TurnHints[0].Rotation==*Scene.HoverMove) ||
                !(Scene.TurnHints[1].Rotation==Scene.HoverMove->Inverse())) throw std::runtime_error("Missing left/right graphical turn hints");
            SmokeHorizontal=*Scene.HoverMove; CaptureSmoke(L".horizontal.bmp");
            SmokeClick(false,false); SmokeStage=8; break;
        case 8:
            if (Locked()) break;
            if (Trail.Cursor()!=1 || Trail.Entries().size()!=1) throw std::runtime_error("Left click was not recorded");
            SmokeClick(true,false); SmokeStage=9; break;
        case 9:
            if (Locked()) break;
            if (Trail.Cursor()!=0 || DrawnHistoryRows!=0 || Trail.Entries().size()!=1 || !Trail.State().IsSolved())
                throw std::runtime_error("Right click must undo rather than append an inverse row");
            if (!Completion.Active()) throw std::runtime_error("Manual solve must trigger completion feedback");
            CaptureSmoke(L".undo.bmp"); SmokeClick(false,false); SmokeStage=10; break;
        case 10:
            if (Locked()) break;
            if (Trail.Cursor()!=1 || Trail.Entries().size()!=1) throw std::runtime_error("Redo duplicated the row");
            SmokeClick(true,false); SmokeStage=11; break;
        case 11:
            if (Locked()) break;
            SmokeControl=true; SmokeStage=12; break;
        case 12:
            if (!Scene.HoverMove || Scene.HoverMove->SameSlice(SmokeHorizontal)) throw std::runtime_error("Ctrl did not select a different cross");
            SmokeVertical=*Scene.HoverMove; CaptureSmoke(L".vertical.bmp");
            SmokeClick(false,true);
            if (Trail.Entries().size()!=1 || !(Trail.Entries()[0].Rotation==SmokeVertical)) throw std::runtime_error("Ctrl preview/click mismatch");
            SmokeStage=13; break;
        case 13:
            if (Locked()) break;
            SmokeClick(true,true); SmokeStage=14; break;
        case 14:
            if (Locked()) break;
            if (Trail.Cursor()!=0 || DrawnHistoryRows!=0 || !Trail.State().IsSolved()) throw std::runtime_error("Ctrl inverse did not rewind");
            SmokeControl=false; SmokeClick(false,false); SmokeStage=15; break;
        case 15:
            if (!Trail.ActiveMove() || Trail.Progress()<=0 || Trail.Progress()>=1) throw std::runtime_error("Missing partial turn for reverse test");
            SmokeClick(true,false); SmokeStage=16; break;
        case 16:
            if (Locked()) break;
            if (Trail.Cursor()!=0 || DrawnHistoryRows!=0 || !Trail.State().IsSolved()) throw std::runtime_error("Mid-animation opposite click did not return to origin");
            SmokeClick(false,false); SmokeClick(true,false);
            if (Trail.Busy() || Trail.HistoryCount()!=0 || Trail.Entries().size()!=1)
                throw std::runtime_error("Opposite clicks in the same frame must cancel the pending turn");
            {
                const auto camera=View;
                SendMessageW(Window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(SmokeClickX,SmokeClickY));
                SendMessageW(Window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(SmokeClickX+30,SmokeClickY+20));
                SendMessageW(Window,WM_LBUTTONUP,0,MAKELPARAM(SmokeClickX+30,SmokeClickY+20));
                if (Trail.Cursor()!=0 || View.Yaw==camera.Yaw) throw std::runtime_error("Drag generated a move or failed to orbit");
                View=camera;
            }
            PointAtRandomButton();
            Status="Controls, history and solver verified. Checking stable UI frames.";
            WarmupFrames=0; SmokeStage=17; break;
        case 17:
            if (!ButtonEvents.empty() || GuiButtons) { WarmupFrames=0; break; }
            if (++WarmupFrames<4) break;
            CaptureSmoke(ResizedSmoke ? L".stable-resized.bmp" : L".stable.bmp");
            StableHash=Graphics.UIHash(LeftWidth,RightWidth); SmokeStage=18; break;
        case 18:
            if (Locked()) throw std::runtime_error("Unexpected action during stationary UI test");
            if (Graphics.UIHash(LeftWidth,RightWidth)!=StableHash)
            { CaptureSmoke(L".unstable.bmp"); throw std::runtime_error("Stationary IMGUI pixels changed between frames"); }
            ++StableFrames;
            if (StableFrames%24!=0) break;
            if (!ResizedSmoke)
            {
                RECT client{0,0,1360,840}; AdjustWindowRect(&client,WS_OVERLAPPEDWINDOW,FALSE);
                SetWindowPos(Window,nullptr,0,0,client.right-client.left,client.bottom-client.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
                ResizedSmoke=true; WarmupFrames=0; SmokeStage=17;
            }
            else { Status="PASS: cube, solver, live history, undo/redo and 48 stable UI frames."; SmokeStage=19; }
            break;
        case 19:
            View.Yaw=2.2f; View.Pitch=-0.45f; MouseX=MouseY=-1;
            SmokeStage=20; break;
        case 20:
            PointAtSmokeSticker(); SmokeStage=21; break;
        case 21:
            if (Scene.TurnHints.size()!=2) throw std::runtime_error("No hints after orbiting to another face");
            SmokeHorizontal=Scene.TurnHints[0].Rotation;
            CaptureSmoke(L".rotated-row.bmp"); SmokeControl=true; SmokeStage=22; break;
        case 22:
            if (Scene.TurnHints.size()!=2 || Scene.TurnHints[0].Rotation.SameSlice(SmokeHorizontal))
                throw std::runtime_error("Ctrl hints must follow the orbited face axes");
            SmokeVertical=Scene.TurnHints[0].Rotation;
            CaptureSmoke(L".rotated-column.bmp"); SmokeClick(false,true);
            if (!(Trail.Entries()[Trail.Cursor()].Rotation==SmokeVertical)) throw std::runtime_error("Rotated hint/click mismatch");
            SmokeStage=23; break;
        case 23:
            if (Locked()) break;
            SmokeClick(true,true); SmokeStage=24; break;
        case 24:
            if (Locked()) break;
            if (!Trail.State().IsSolved() || !Completion.Active()) throw std::runtime_error("Rotated cube undo/celebration failed");
            if (Completion.Age()<0.65f) break;
            CaptureSmoke(L".rotated-solved.bmp");
            View=ViewSettings{}; MouseX=MouseY=-1; SmokeControl=false;
            SmokeStage=25; break;
        case 25:
            CaptureSmoke(L""); SmokeDone=true; PostQuitMessage(0); break;
        }
    }
};

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* app = reinterpret_cast<Application*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        app = static_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app)); app->Window = window;
    }
    if (!app) return DefWindowProcW(window,message,wParam,lParam);
    switch (message)
    {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT paint{}; BeginPaint(window,&paint); EndPaint(window,&paint); return 0;
    }
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize = {1100,700}; return 0;
    case WM_SIZE:
        app->Minimized = wParam == SIZE_MINIMIZED;
        if (!app->Minimized && !app->Graphics.Resize(LOWORD(lParam),HIWORD(lParam))) PostQuitMessage(1);
        return 0;
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN:
        app->MouseX = GET_X_LPARAM(lParam); app->MouseY = GET_Y_LPARAM(lParam);
        if (message == WM_LBUTTONDOWN) app->Buttons |= IMGUI_MBUT_LEFT; else app->Buttons |= IMGUI_MBUT_RIGHT;
        app->ButtonEvents.push_back({app->Buttons,app->MouseX,app->MouseY});
        if (app->OverScene(app->MouseX) && !app->Pressed)
        {
            app->Pressed = true; app->Dragged = false; app->Right = message == WM_RBUTTONDOWN;
            app->Ctrl = (wParam&MK_CONTROL)!=0;
            app->DownX = app->LastX = app->MouseX; app->DownY = app->LastY = app->MouseY;
        }
        SetCapture(window); return 0;
    case WM_MOUSEMOVE:
    {
        app->MouseX = GET_X_LPARAM(lParam); app->MouseY = GET_Y_LPARAM(lParam);
        if (app->Pressed)
        {
            const int dx = app->MouseX-app->DownX, dy = app->MouseY-app->DownY;
            if (dx*dx+dy*dy > 25) app->Dragged = true;
            if (app->Dragged)
            {
                app->LastClickMove.reset();
                app->View.Yaw += (app->MouseX-app->LastX)*0.01f;
                app->View.Pitch = std::clamp(app->View.Pitch+(app->MouseY-app->LastY)*0.01f,-1.5f,1.5f);
            }
            app->LastX = app->MouseX; app->LastY = app->MouseY;
        }
        return 0;
    }
    case WM_LBUTTONUP: case WM_RBUTTONUP:
    {
        const bool right = message == WM_RBUTTONUP;
        app->MouseX = GET_X_LPARAM(lParam); app->MouseY = GET_Y_LPARAM(lParam);
        app->Buttons &= ~(right ? IMGUI_MBUT_RIGHT : IMGUI_MBUT_LEFT);
        app->ButtonEvents.push_back({app->Buttons,app->MouseX,app->MouseY});
        if (app->Pressed && right == app->Right)
        {
            if (!app->Dragged) app->Click(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam),right,app->Ctrl);
            app->Pressed = false;
        }
        if (!app->Buttons) ReleaseCapture(); return 0;
    }
    case WM_MOUSEWHEEL:
    {
        POINT point{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)}; ScreenToClient(window,&point);
        if (app->OverScene(point.x)) app->View.Zoom = std::clamp(app->View.Zoom*std::pow(1.12f,GET_WHEEL_DELTA_WPARAM(wParam)/120.0f),0.2f,4.0f);
        else
        {
            app->Wheel -= GET_WHEEL_DELTA_WPARAM(wParam)/WHEEL_DELTA;
            if (point.x>=app->Graphics.Width()-RightWidth) app->FollowHistory=false;
        }
        return 0;
    }
    case WM_CAPTURECHANGED: case WM_KILLFOCUS: app->CancelGesture(); return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { app->Cancel = true; app->DemoRewind = false; app->Trail.StopAtNextBoundary(); }
        if (wParam == VK_SPACE && !app->Solving && !(lParam & (1 << 30)))
        { app->DemoRewind = false; if (app->Trail.Busy()) app->Trail.StopAtNextBoundary(); else app->Trail.Seek(app->Trail.Entries().size()); }
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: return DefWindowProcW(window,message,wParam,lParam);
    }
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    bool smoke = false;
    try
    {
        SetProcessDPIAware();
        Application app;
        int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(),&argc);
        bool warp = false; int order = 3;
        for (int i = 1; i < argc; ++i)
        {
            const std::wstring arg = argv[i];
            if (arg == L"--smoke") app.Smoke = smoke = true;
            else if (arg == L"--warp") warp = true;
            else if ((arg == L"--order" || arg == L"--dimension") && i+1 < argc) order = std::stoi(argv[++i]);
            else if (arg == L"--capture" && i+1 < argc) app.Capture = argv[++i];
            else throw std::invalid_argument("Unknown argument or missing value");
        }
        LocalFree(argv); app.ChangeOrder(order);
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = WindowProc; wc.hInstance = instance;
        wc.hCursor = LoadCursor(nullptr,IDC_ARROW); wc.lpszClassName = L"BurnsideNDWindow";
        if (!RegisterClassExW(&wc)) throw std::runtime_error("Window class registration failed");
        RECT rect{0,0,1560,960}; AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW,FALSE);
        HWND window = CreateWindowExW(0,wc.lpszClassName,L"Burnside | NxNxN Rubik Cube Solver",WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,&app);
        if (!window) throw std::runtime_error("Window creation failed");
        if (!app.Graphics.Initialize(window,warp)) throw std::runtime_error(app.Graphics.Error());
        ShowWindow(window,app.Smoke ? SW_HIDE : show);
        auto previous = std::chrono::steady_clock::now();
        bool running = true; int result = 0;
        while (running)
        {
            MSG msg{};
            while (PeekMessageW(&msg,nullptr,0,0,PM_REMOVE))
            {
                if (msg.message == WM_QUIT) { running = false; result = static_cast<int>(msg.wParam); break; }
                TranslateMessage(&msg); DispatchMessageW(&msg);
            }
            if (!running) break;
            if (app.Minimized) { WaitMessage(); previous = std::chrono::steady_clock::now(); continue; }
            const auto now = std::chrono::steady_clock::now();
            float dt = std::chrono::duration<float>(now-previous).count(); previous = now;
            app.Tick(app.Smoke ? 0.016f : std::min(dt,0.1f));
            app.Scene = BuildScene(app.Trail,app.View,app.Graphics.Width(),app.Graphics.Height(),LeftWidth,RightWidth,app.MouseX,app.MouseY,app.Smoke ? app.SmokeControl : (GetKeyState(VK_CONTROL)&0x8000)!=0);
            app.GUI();
            if (!app.Graphics.Draw(app.Scene)) throw std::runtime_error(app.Graphics.Error().empty() ? "Render failed" : app.Graphics.Error());
            if (app.Smoke) app.SmokeTick();
            app.ExecuteDeferred();
        }
        if (app.Smoke && !app.SmokeDone) result = 1;
        if (IsWindow(window)) DestroyWindow(window);
        if (app.Smoke && !app.Capture.empty())
        {
            std::ofstream report(std::filesystem::path(app.Capture+L".txt"));
            report << (result == 0 ? "PASS" : "FAIL") << " order=" << order << " frames=" << app.SmokeFrames
                   << " MSAA=" << app.Graphics.SampleCount() << " stable_ui_frames=" << app.StableFrames
                   << " random_sync_frames=" << app.RandomSyncFrames << '\n' << app.Status << '\n';
        }
        return result;
    }
    catch (const std::exception& e)
    {
        OutputDebugStringA(e.what());
        if (!smoke) MessageBoxA(nullptr,e.what(),"Burnside error",MB_ICONERROR);
        else { std::ofstream error("Burnside-smoke-error.txt"); error << e.what(); }
        return 1;
    }
}
