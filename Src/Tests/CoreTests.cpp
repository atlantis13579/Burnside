#include "Core/Solver.h"
#include "Core/Timeline.h"
#include "App/Scene.h"
#include "App/Feedback.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace Burnside;
namespace
{
size_t checks=0;
void Check(bool condition,const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
template<class F> void Throws(F action)
{
    bool threw=false; try { action(); } catch (const std::exception&) { threw=true; }
    Check(threw,"Expected invalid input to be rejected");
}
void Algebra()
{
    Throws([] { Cube invalid(1); }); Throws([] { Cube invalid(17); });
    Check(TimeSeed()!=TimeSeed(),"Consecutive time seeds must differ");
    for (int n=2; n<=MaxOrder; ++n)
    {
        Cube cube(n),solved=cube;
        const auto& layout=cube.Geometry();
        Check(cube.Colors().size()==static_cast<size_t>(6*n*n),"NxNxN sticker count");
        Check(layout.FaceSize()==static_cast<size_t>(n*n),"Each face has NxN stickers");
        Check(layout.Generators().size()==static_cast<size_t>(6*n),"Every layer has both quarter turns");
        for (size_t i=0; i<layout.Stickers().size(); ++i)
        {
            const auto& s=layout.Stickers()[i];
            Check(layout.Index(s)==i,"Sticker address round trip");
            Check(s.Position[s.Axis]==(s.Sign>0 ? n-1 : 0),"Sticker lies on its physical outer face");
        }
        for (const Move& m : layout.Generators())
        {
            std::vector<bool> visited(cube.Colors().size());
            size_t changed=0;
            for (size_t i=0; i<visited.size(); ++i)
            {
                const size_t destination=layout.Destination(i,m);
                Check(destination<visited.size() && !visited[destination],"Every slice turn is a permutation");
                visited[destination]=true;
                if (layout.Stickers()[i].Position[m.Axis]!=m.Layer) Check(destination==i,"Unselected layers remain fixed");
                changed+=destination!=i;
            }
            Check(changed>=static_cast<size_t>(4*n),"Every layer, including inner layers, turns real stickers");
            cube.Apply(m); cube.Apply(m.Inverse()); Check(cube==solved,"Slice inverse restores state");
            for (int q=0; q<4; ++q) cube.Apply(m);
            Check(cube==solved,"Four quarter turns restore every order");
        }
        Sticker corner; corner.Axis=2; corner.Sign=1; corner.Position={0,0,static_cast<int8_t>(n-1)};
        const Move front{2,n-1,0,1,1};
        const Sticker& destination=layout.Stickers()[layout.Destination(layout.Index(corner),front)];
        Check(destination.Position[0]==n-1 && destination.Position[1]==0 && destination.Position[2]==n-1,"Known front-face corner rotation");
        auto path=Scramble(n,40,static_cast<uint32_t>(100+n));
        Check(path==Scramble(n,40,static_cast<uint32_t>(100+n)),"Explicit seed remains deterministic");
        cube.Apply(path); Check(!cube.IsSolved(),"Scramble must change the cube");
        auto colors=cube.Colors(); std::sort(colors.begin(),colors.end());
        Check(colors==solved.Colors(),"Six face color populations are conserved");
        const auto answer=Solve(cube,{},&path);
        Check(answer.Success(),"All orders have a certified solution");
        cube.Apply(answer.Moves); Check(cube.IsSolved(),"Solution replay restores all orders");
        auto undo=Invert(path); path.insert(path.end(),undo.begin(),undo.end());
        Check(Reduce(path,n).empty(),"Inverse path reduction");
        Move a{0,0,1,2,1},b{0,n-1,1,2,1};
        Check(Reduce({a,b,a.Inverse(),b.Inverse()},n).empty(),"Disjoint slices commute");
        Throws([&] { cube.Apply({0,n,1,2,1}); });
        Throws([&] { cube.Apply({0,-1,1,2,1}); });
        Throws([&] { cube.Apply({3,0,0,1,1}); });
        SaveState(cube,"../Intermediate/Test.state");
        Check(LoadState("../Intermediate/Test.state")==cube,"State round trip for every order");
        SaveMoves({n,{a,b}},"../Intermediate/Test.moves");
        const auto loaded=LoadMoves("../Intermediate/Test.moves");
        Check(loaded.Order==n && loaded.Moves==std::vector<Move>{a,b},"Sequence round trip for every order");
        std::cout << n << "x" << n << "x" << n << " algebra, all layers and solve passed\n";
    }
}
void Search()
{
    for (int n=2; n<=MaxOrder; ++n)
    {
        Cube cube(n); cube.Apply(Scramble(n,n<=4 ? 3 : 2,97));
        SolveOptions options; options.MaxDepth=6; options.TimeLimit=std::chrono::seconds(10);
        const auto answer=Solve(cube,options);
        Check(answer.Success(),"State-only search for every order");
        cube.Apply(answer.Moves); Check(cube.IsSolved(),"State-only answer replay");
    }
    Cube cube(3); const Move move{0,2,1,2,1}; cube.Apply(move);
    SolveOptions options; options.MaxDepth=0;
    Check(Solve(cube,options).Status==SolveStatus::DepthLimit,"Depth limit result");
    options.MaxDepth=6; options.MaxStates=2;
    Check(Solve(cube,options).Status==SolveStatus::BudgetExceeded,"Node budget result");
    options.MaxStates=10000; options.TimeLimit=std::chrono::milliseconds(0);
    Check(Solve(cube,options).Status==SolveStatus::BudgetExceeded,"Time budget result");
    std::atomic_bool cancel{true}; options.Cancel=&cancel;
    Check(Solve(cube,options).Status==SolveStatus::Cancelled,"Cancellation");
    options.Cancel=nullptr; options.TimeLimit=std::chrono::seconds(5);
    std::vector<Move> stale{{1,2,0,2,1}};
    const auto answer=Solve(cube,options,&stale);
    Check(answer.Success() && answer.Method.find("State-only")!=std::string::npos,"Stale certificate rejected");
    const Cube before=cube; Solve(cube,options); Check(cube==before,"Solver input immutability");
    auto invalid=cube.Colors(); invalid[0]=6; Throws([&] { cube.SetColors(invalid); });
    std::ofstream legacy("../Intermediate/Legacy.moves"); legacy << "BURNSIDE_MOVES 1 4\n"; legacy.close();
    Throws([] { LoadMoves("../Intermediate/Legacy.moves"); });
}
void TwoPhase()
{
    SolveOptions options; options.TimeLimit=std::chrono::seconds(15);
    for (int order : {2,3})
        for (uint32_t seed : {1u,42u,2026u})
        {
            Cube cube(order); cube.Apply(Scramble(order,25,seed));
            Cube independent(order); independent.SetColors(cube.Colors());
            const auto answer=Solve(independent,SolveMethod::TwoPhase,options);
            Check(answer.Success() && !answer.Moves.empty(),"Two-phase solves without a move history");
            independent.Apply(answer.Moves);
            Check(independent.IsSolved(),"Two-phase solution replays on the sticker model");
        }
    Cube middle(3); middle.Apply({0,1,1,2,1});
    const auto oriented=Solve(middle,SolveMethod::TwoPhase,options);
    Check(oriented.Success(),"Two-phase normalizes a middle-slice center rotation");
    middle.Apply(oriented.Moves); Check(middle.IsSolved(),"Middle-slice solution replays");
    Cube large(4); large.Apply({0,0,1,2,1});
    Check(Solve(large,SolveMethod::TwoPhase,options).Status==SolveStatus::UnsupportedState,
          "Two-phase reports unsupported cube orders");
    Cube impossible(3); auto colors=impossible.Colors();
    std::swap(colors[0],colors[9]); impossible.SetColors(colors);
    Check(Solve(impossible,SolveMethod::TwoPhase,options).Status==SolveStatus::UnsupportedState,
          "Two-phase rejects an impossible sticker arrangement");
    Cube noHistory(3); noHistory.Apply({0,0,1,2,1});
    Check(Solve(noHistory,SolveMethod::History,options).Status==SolveStatus::InvalidCertificate,
          "Explicit history method requires a certificate");
    options.MaxTwoPhaseNodes=1;
    Check(Solve(noHistory,SolveMethod::TwoPhase,options).Status==SolveStatus::BudgetExceeded,
          "Two-phase honors its search budget");
}
void Reduction()
{
    SolveOptions options; options.TimeLimit=std::chrono::seconds(15);
    std::atomic<int> stage{0}; options.ProgressStage=&stage;
    for (int order=4; order<=MaxOrder; ++order)
    {
        stage=0;
        Cube scrambled(order); scrambled.Apply(Scramble(order,25,static_cast<uint32_t>(order+42)));
        Cube independent(order); independent.SetColors(scrambled.Colors());
        const auto answer=Solve(independent,SolveMethod::Reduction,options);
        Check(answer.Success() && !answer.Moves.empty(),"NxN reduction solves without move history");
        Check(stage==6,"NxN reduction reports verification stage");
        independent.Apply(answer.Moves);
        Check(independent.IsSolved(),"NxN reduction replays on the sticker model");
    }
    Cube small(3); small.Apply({0,0,1,2,1});
    Check(Solve(small,SolveMethod::Reduction,options).Status==SolveStatus::UnsupportedState,
          "Reduction reports unsupported orders");
}
void StagedReduction()
{
    SolveOptions options; options.TimeLimit=std::chrono::seconds(15);
    std::atomic<int> stage{0}; options.ProgressStage=&stage;
    for (int order : {4,6,8,10,12,14,16})
    {
        Cube scrambled(order); scrambled.Apply(Scramble(order,25,static_cast<uint32_t>(order+97)));
        for (SolveMethod method : {SolveMethod::Yau,SolveMethod::Hoya})
        {
            stage=0;
            Cube independent(order); independent.SetColors(scrambled.Colors());
            const auto answer=Solve(independent,method,options);
            Check(answer.Success() && !answer.Moves.empty(),"Staged reduction solves without move history");
            Check(stage==7,"Staged reduction reports verification stage");
            independent.Apply(answer.Moves);
            Check(independent.IsSolved(),"Staged reduction replays on the sticker model");
        }
    }
    Cube odd(5); odd.Apply({0,1,1,2,1});
    Check(Solve(odd,SolveMethod::Yau,options).Status==SolveStatus::UnsupportedState,
          "Yau-style staging reports unsupported odd orders");
    Check(Solve(odd,SolveMethod::Hoya,options).Status==SolveStatus::UnsupportedState,
          "Hoya-style staging reports unsupported odd orders");
    Cube limited(4); limited.Apply(Scramble(4,25,46));
    const Cube before=limited;
    options.MaxTwoPhaseNodes=0;
    for (SolveMethod method : {SolveMethod::Reduction,SolveMethod::Yau,SolveMethod::Hoya})
    {
        stage=0;
        const auto answer=Solve(limited,method,options);
        Check(stage==(method==SolveMethod::Reduction ? 3 : 6),
              "Reduction methods reach the shared two-phase finish");
        Check(answer.Status==SolveStatus::BudgetExceeded && answer.Visited==1,
              "Reduction methods propagate the two-phase node budget");
        Check(answer.Moves.empty() && limited==before,
              "An exhausted reduction does not publish a partial solution or mutate its input");
    }
}
void History()
{
    const Move timingMove{1,0,0,2,1};
    Timeline randomTiming(3),manualTiming(3);
    randomTiming.Append({timingMove},MoveSource::Scramble);
    manualTiming.Append({timingMove},MoveSource::Manual);
    randomTiming.Tick(0.05f,0.3f,3.0f);
    manualTiming.Tick(0.05f,0.3f,3.0f);
    Check(std::abs(randomTiming.Progress()-0.5f)<1e-5f &&
          std::abs(manualTiming.Progress()-1.0f/6.0f)<1e-5f,
          "Random animation uses three times the global turn speed");
    randomTiming.ResetImmediate();
    Check(randomTiming.State().IsSolved() && randomTiming.Entries().empty() &&
          randomTiming.Cursor()==0 && randomTiming.Target()==0 &&
          randomTiming.HistoryCount()==0 && !randomTiming.Busy() && !randomTiming.ActiveMove(),
          "Immediate reset clears an active turn and all future history");
    for (int n=2; n<=MaxOrder; ++n)
    {
        Timeline timeline(n); const auto path=Scramble(n,6,93);
        timeline.Append(path,MoveSource::Scramble);
        Check(timeline.HistoryCount()==0 && timeline.Entries().size()==6,"Queued random moves are not history yet");
        timeline.Tick(0.1f,0.2f);
        Check(timeline.HistoryCount()==1 && timeline.Cursor()==0 && timeline.State().IsSolved(),"First row appears during first animation");
        Check(*timeline.ActiveMove()==path[0],"Live row matches the rendered move");
        timeline.Tick(0.1f,0.2f);
        Check(timeline.Cursor()==1 && timeline.HistoryCount()==1,"Completing first move does not expose remaining queue");
        timeline.Tick(10,0.2f);
        Cube expected(n); expected.Apply(path);
        Check(timeline.Cursor()==6 && timeline.HistoryCount()==6 && timeline.State()==expected,"Random playback matches history");
        timeline.Seek(2); timeline.Tick(0.05f,0.2f);
        Check(timeline.ActiveMove().has_value(),"History seeks animate");
        timeline.Tick(10,0.2f); expected.Reset(); expected.Apply({path[0],path[1]});
        Check(timeline.Cursor()==2 && timeline.HistoryCount()==2 && timeline.State()==expected,"Rewind hides future rows");
        Check(timeline.ApplyManual(path[1].Inverse())==ManualAction::Undo,"Opposite click is undo");
        timeline.Tick(10,0.2f);
        Check(timeline.Cursor()==1 && timeline.HistoryCount()==1 && timeline.Entries().size()==6,"Undo does not append an inverse entry");
        Check(timeline.ApplyManual(path[1])==ManualAction::Redo,"Matching future click is redo");
        timeline.Tick(10,0.2f); Check(timeline.Cursor()==2 && timeline.Entries().size()==6,"Redo reuses existing row");
        Move branch=path[0];
        for (const auto& move : timeline.State().Geometry().Generators())
            if (!(move==path[1].Inverse()) && !(move==path[2])) { branch=move; break; }
        Check(timeline.ApplyManual(branch)==ManualAction::Appended,"Different move creates branch");
        timeline.Tick(10,0.2f); Check(timeline.Entries().size()==3,"Branch truncates abandoned future");
        timeline.ResetAnimated(); timeline.Tick(10,0.2f);
        Check(timeline.State().IsSolved() && timeline.Entries().empty() && timeline.HistoryCount()==0,"Animated reset clears history");
        const Move manual{1,n/2,0,2,-1};
        timeline.ApplyManual(manual);
        Check(timeline.ApplyManual(manual.Inverse())==ManualAction::Undo,"Opposite input before first frame cancels the pending turn");
        Check(!timeline.Busy() && timeline.Cursor()==0 && timeline.HistoryCount()==0,"Unstarted cancellation keeps history and state at origin");
        timeline.ApplyManual(manual); timeline.Tick(0.1f,0.2f);
        const float progress=timeline.Progress();
        Check(timeline.ApplyManual(manual.Inverse())==ManualAction::Undo,"Opposite input during animation reverses it");
        Check(timeline.Progress()==progress,"Reversing does not jump the pose");
        timeline.Tick(0.05f,0.2f);
        Check(timeline.Progress()<progress && timeline.Progress()>0,"Reverse animation travels continuously backward");
        timeline.Tick(1,0.2f);
        Check(timeline.Cursor()==0 && timeline.HistoryCount()==0 && timeline.State().IsSolved(),"In-flight undo restores origin without committing");
        Check(timeline.ApplyManual(manual)==ManualAction::Redo,"Cancelled move remains redoable");
        timeline.Tick(1,0.2f); Check(timeline.Cursor()==1 && timeline.Entries().size()==1,"Redo does not duplicate cancelled move");
        Throws([&] { timeline.Seek(99); }); Throws([&] { timeline.Tick(1,0); });
    }
}
void Geometry()
{
    ViewSettings front; front.Yaw=0; front.Pitch=0;
    ViewSettings original=front; original.Zoom=1;
    const auto reduced=ProjectPoint({1,1,1},front,1560,960,290,340).Position;
    const auto previous=ProjectPoint({1,1,1},original,1560,960,290,340).Position;
    const auto cameraCenter=ProjectPoint({},front,1560,960,290,340).Position;
    Check(std::abs((reduced.X-cameraCenter.X)/(previous.X-cameraCenter.X)-0.8f)<1e-5f &&
          std::abs((reduced.Y-cameraCenter.Y)/(previous.Y-cameraCenter.Y)-0.8f)<1e-5f,"Default projected size is exactly 20 percent smaller");
    for (const auto& size : std::vector<std::pair<int,int>>{{1560,960},{1360,840},{2000,800},{1100,900}})
    {
        const auto origin=ProjectPoint({},front,size.first,size.second,290,340);
        const auto x=ProjectPoint({1,0,0},front,size.first,size.second,290,340);
        const auto y=ProjectPoint({0,1,0},front,size.first,size.second,290,340);
        const float dx=(x.Position.X-origin.Position.X)*size.first*0.5f;
        const float dy=(y.Position.Y-origin.Position.Y)*size.second*0.5f;
        Check(std::abs(dx-dy)<0.001f,"Perspective camera preserves pixel aspect at every window shape");
        const auto near=ProjectPoint({1,0,1},front,size.first,size.second,290,340);
        Check(near.Position.X>x.Position.X && near.ClipW<x.ClipW,"Camera uses perspective depth, not a flattened projection");
    }
    for (int n=2; n<=MaxOrder; ++n)
    {
        Timeline timeline(n); ViewSettings view;
        auto scene=BuildScene(timeline,view,1560,960,290,340,-1,-1);
        Check(scene.RenderedCubies==static_cast<size_t>(n*n*n-(n-2)*(n-2)*(n-2)),"Render omits hidden interior cubies");
        Check(scene.Vertices.size()<static_cast<size_t>(n*n*n*324),"Surface rendering stays below full cubie geometry");
        Check(scene.VisibleStickers==static_cast<size_t>(6*n*n),"Render contains all six NxN faces");
        for (const Vertex& v : scene.Vertices)
            Check(std::isfinite(v.Position.X) && std::isfinite(v.Position.Y) && std::isfinite(v.Position.Z) && v.ClipW>0,"Finite perspective geometry");
        for (int row=0; row<n; ++row) for (int column=0; column<n; ++column)
        {
            Sticker s; s.Axis=2; s.Sign=1; s.Position={static_cast<int8_t>(column),static_cast<int8_t>(row),static_cast<int8_t>(n-1)};
            const auto index=timeline.State().Geometry().Index(s);
            const Move horizontal=ClickMove(timeline.State().Geometry(),index,front,false,true);
            const Move vertical=ClickMove(timeline.State().Geometry(),index,front,true,true);
            Check(horizontal.Axis==1 && horizontal.Layer==row && horizontal.A==0 && horizontal.B==2,"Every row cross selects the clicked row");
            Check(vertical.Axis==0 && vertical.Layer==column && vertical.A==1 && vertical.B==2,"Every column cross selects the clicked column");
            Check(horizontal.Inverse()==ClickMove(timeline.State().Geometry(),index,front,false,false),"Right click is the inverse");
        }
        Sticker selected; selected.Axis=2; selected.Sign=1;
        selected.Position={static_cast<int8_t>(n/2),static_cast<int8_t>(n/2),static_cast<int8_t>(n-1)};
        const size_t index=timeline.State().Geometry().Index(selected);
        const Vec3 center=scene.StickerCenters[index];
        const int mx=static_cast<int>(std::lround((center.X+1)*780)),my=static_cast<int>(std::lround((1-center.Y)*480));
        const auto picked=BuildScene(timeline,view,1560,960,290,340,mx,my);
        const auto alternate=BuildScene(timeline,view,1560,960,290,340,mx,my,true);
        Check(picked.HoveredSticker==static_cast<int>(index),"Perspective depth picking selects the visible sticker");
        Check(picked.HoverMove && alternate.HoverMove && !picked.HoverMove->SameSlice(*alternate.HoverMove),"Row/column previews differ");
        size_t highlighted=0; for (const auto& v : picked.Vertices) highlighted+=v.Highlight>0;
        Check(highlighted>6 && highlighted<picked.Vertices.size(),"Selection highlights a whole cross, not the entire cube");
        const Move turn{1,n/2,0,2,-1}; timeline.Append({turn},MoveSource::Manual); timeline.Tick(0.1f,0.2f);
        const auto animated=BuildScene(timeline,view,1560,960,290,340,-1,-1);
        const float pitch=3.12f/n,middle=(n-1)*0.5f;
        Vec3 p{(selected.Position[0]-middle)*pitch,(selected.Position[1]-middle)*pitch,(selected.Position[2]-middle+0.482f)*pitch};
        const float c=std::sqrt(0.5f); const Vec3 rotated{c*(p.X+p.Z),p.Y,c*(p.Z-p.X)};
        const Vec3 expected=ProjectPoint(rotated,view,1560,960,290,340).Position;
        Check(std::abs(animated.StickerCenters[index].X-expected.X)<1e-5f && std::abs(animated.StickerCenters[index].Y-expected.Y)<1e-5f,"Every order renders the correct halfway slice rotation");
        std::cout << n << "x" << n << "x" << n << " mesh, picking and animation passed\n";
    }
}
void Feedback()
{
    CompletionEffect effect;
    effect.Tick(true,false,1);
    Check(!effect.Active(),"Initial solved state does not celebrate");
    effect.Tick(true,true,0.1f); effect.Tick(true,false,0.1f);
    Check(!effect.Active(),"Cancelling an uncommitted turn does not claim a solve");
    effect.Tick(false,true,0.1f); effect.Tick(true,true,0.1f);
    Check(!effect.Active(),"Completion waits for the animation queue to finish");
    effect.Tick(true,false,0.1f);
    Check(effect.Active() && effect.TriggerCount()==1,"Restoring a scrambled cube triggers feedback");
    effect.Tick(true,false,0.5f);
    Check(effect.TriggerCount()==1 && effect.Age()>0,"A solved idle cube does not retrigger feedback");
    effect.Tick(true,false,CompletionEffect::Duration);
    Check(!effect.Active(),"Completion effect expires");
    effect.Tick(false,false,0.1f); effect.Reset(true);
    effect.Tick(false,true,0.1f); effect.Tick(true,false,0.1f);
    Check(!effect.Active() && effect.TriggerCount()==1,"Reset is not a solve reward");
    effect.Tick(false,true,0.1f); effect.Tick(true,false,0.1f);
    Check(effect.Active() && effect.TriggerCount()==2,"A later genuine solve can celebrate again");
    effect.Tick(true,true,0.1f);
    Check(!effect.Active(),"A new move dismisses the celebration");

    for (int n : {2,3,10,16})
    {
        Timeline timeline(n); timeline.Append(Scramble(n,5,72),MoveSource::Scramble); timeline.Tick(10,0.2f);
        for (const auto angles : std::vector<std::pair<float,float>>{{-0.58f,0.4f},{2.2f,-0.45f},{-2.6f,1.0f},{0.4f,-1.1f}})
        {
            ViewSettings view; view.Yaw=angles.first; view.Pitch=angles.second;
            const auto base=BuildScene(timeline,view,1560,960,290,340,-1,-1);
            int visible=0;
            for (int axis=0; axis<3; ++axis) for (int sign : {-1,1})
            {
                Sticker s; s.Axis=static_cast<uint8_t>(axis); s.Sign=static_cast<int8_t>(sign);
                s.Position.fill(static_cast<int8_t>(n/2)); s.Position[axis]=static_cast<int8_t>(sign>0 ? n-1 : 0);
                const auto index=timeline.State().Geometry().Index(s);
                const Vec3 start=base.StickerCenters[index];
                const int mx=static_cast<int>(std::lround((start.X+1)*780)),my=static_cast<int>(std::lround((1-start.Y)*480));
                for (bool control : {false,true})
                {
                    const auto hovered=BuildScene(timeline,view,1560,960,290,340,mx,my,control);
                    if (hovered.HoveredSticker!=static_cast<int>(index)) continue;
                    ++visible;
                    Check(hovered.TurnHints.size()==2,"Both mouse directions have graphical hints on each visible rotated face");
                    for (size_t button=0; button<2; ++button)
                    {
                        const auto& hint=hovered.TurnHints[button];
                        Check(hint.Rotation==ClickMove(timeline.State().Geometry(),index,view,control,button==0),"Hint represents the actual selected mouse action");
                        Check(std::abs(hint.Path[0].X-start.X)<1e-6f && std::abs(hint.Path[0].Y-start.Y)<1e-6f,"Turn arrow starts on the picked sticker after orbit and scramble");
                        Timeline moving=timeline; moving.Append({hint.Rotation},MoveSource::Manual); moving.Tick(0.01f,0.2f);
                        const auto animation=BuildScene(moving,view,1560,960,290,340,mx,my,control);
                        const Vec3 a=animation.StickerCenters[index]-start,b=hint.Path[1]-start;
                        const float dot=a.X*b.X+a.Y*b.Y;
                        const float lengths=std::sqrt((a.X*a.X+a.Y*a.Y)*(b.X*b.X+b.Y*b.Y));
                        Check(lengths>1e-9f && dot/lengths>0.99f,"Projected hint direction aligns with the real animated turn");
                        Check(animation.TurnHints.empty(),"No idle turn arrows during active animation");
                    }
                }
            }
            Check(visible>=2,"Rotated camera tests cover visible cube faces");
        }
    }
}
}
int main()
{
    try { Algebra(); Search(); TwoPhase(); Reduction(); StagedReduction(); History(); Geometry(); Feedback(); std::cout << "PASS: " << checks << " checks\n"; return 0; }
    catch (const std::exception& e) { std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n'; return 1; }
}
