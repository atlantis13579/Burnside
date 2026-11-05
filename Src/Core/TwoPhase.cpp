#include "SolverMethods.h"
#include <algorithm>
#include <array>
#include <queue>
#include <stdexcept>
#include <unordered_set>

namespace Burnside
{
namespace
{
constexpr int U=0, R=1, F=2, D=3, L=4, B=5;
constexpr std::array<int,6> FaceColor{3,1,5,2,0,4};
constexpr std::array<std::array<int,3>,8> CornerFaces{{
    {{U,R,F}},{{U,F,L}},{{U,L,B}},{{U,B,R}},
    {{D,F,R}},{{D,L,F}},{{D,B,L}},{{D,R,B}}
}};
constexpr std::array<std::array<int,2>,12> EdgeFaces{{
    {{U,R}},{{U,F}},{{U,L}},{{U,B}},{{D,R}},{{D,F}},
    {{D,L}},{{D,B}},{{F,R}},{{F,L}},{{B,L}},{{B,R}}
}};
constexpr int Factorial[]{1,1,2,6,24,120,720,5040,40320};

struct Cubie
{
    std::array<uint8_t,8> Cp{}, Co{};
    std::array<uint8_t,12> Ep{}, Eo{};
};
struct Effect { Cubie State; Move Rotation; int Face=0; };

int FaceAxis(int face) { return FaceColor[face]/2; }
int FaceSign(int face) { return FaceColor[face]%2 ? 1 : -1; }
Sticker AtPosition(int order, const int* faces, int faceCount, int targetFace)
{
    Sticker sticker;
    sticker.Axis=static_cast<uint8_t>(FaceAxis(targetFace));
    sticker.Sign=static_cast<int8_t>(FaceSign(targetFace));
    sticker.Position.fill(static_cast<int8_t>((order-1)/2));
    for (int i=0; i<faceCount; ++i)
    {
        const int axis=FaceAxis(faces[i]);
        sticker.Position[axis]=static_cast<int8_t>(FaceSign(faces[i])>0 ? order-1 : 0);
    }
    return sticker;
}
bool SameColors(const uint8_t* actual, const uint8_t* expected, int count)
{
    for (int i=0; i<count; ++i)
    {
        bool found=false;
        for (int j=0; j<count; ++j) found |= actual[i]==expected[j];
        if (!found) return false;
    }
    return true;
}
bool Decode(const Cube& cube, Cubie& out)
{
    const int order=cube.Order();
    if (order!=2 && order!=3) return false;
    int usedCorners=0, usedEdges=0;
    for (int position=0; position<8; ++position)
    {
        uint8_t colors[3];
        for (int i=0; i<3; ++i)
            colors[i]=cube.Colors()[cube.Geometry().Index(AtPosition(order,CornerFaces[position].data(),3,CornerFaces[position][i]))];
        int piece=-1;
        for (int candidate=0; candidate<8; ++candidate)
        {
            uint8_t reference[3];
            for (int i=0; i<3; ++i) reference[i]=static_cast<uint8_t>(FaceColor[CornerFaces[candidate][i]]);
            if (SameColors(colors,reference,3)) { piece=candidate; break; }
        }
        if (piece<0 || (usedCorners&(1<<piece))) return false;
        usedCorners|=1<<piece; out.Cp[position]=static_cast<uint8_t>(piece);
        int orientation=0;
        while (orientation<3 && colors[orientation]!=FaceColor[CornerFaces[piece][0]]) ++orientation;
        if (orientation==3) return false;
        if (colors[(orientation+1)%3]!=FaceColor[CornerFaces[piece][1]] ||
            colors[(orientation+2)%3]!=FaceColor[CornerFaces[piece][2]]) return false;
        out.Co[position]=static_cast<uint8_t>(orientation);
    }
    if (order==3)
    {
        for (int position=0; position<12; ++position)
        {
            uint8_t colors[2];
            for (int i=0; i<2; ++i)
                colors[i]=cube.Colors()[cube.Geometry().Index(AtPosition(order,EdgeFaces[position].data(),2,EdgeFaces[position][i]))];
            int piece=-1;
            for (int candidate=0; candidate<12; ++candidate)
            {
                uint8_t reference[2];
                for (int i=0; i<2; ++i) reference[i]=static_cast<uint8_t>(FaceColor[EdgeFaces[candidate][i]]);
                if (SameColors(colors,reference,2)) { piece=candidate; break; }
            }
            if (piece<0 || (usedEdges&(1<<piece))) return false;
            usedEdges|=1<<piece; out.Ep[position]=static_cast<uint8_t>(piece);
            out.Eo[position]=static_cast<uint8_t>(colors[0]!=FaceColor[EdgeFaces[piece][0]]);
        }
    }
    else for (int i=0; i<12; ++i) out.Ep[i]=static_cast<uint8_t>(i);
    int twist=0, flip=0;
    for (auto v:out.Co) twist+=v;
    for (auto v:out.Eo) flip+=v;
    if (twist%3 || flip%2) return false;
    const auto parity=[](const auto& permutation)
    {
        int value=0;
        for (size_t i=0; i<permutation.size(); ++i)
            for (size_t j=i+1; j<permutation.size(); ++j) value^=permutation[i]>permutation[j];
        return value;
    };
    if (order==2 && parity(out.Cp)!=parity(out.Ep)) std::swap(out.Ep[0],out.Ep[1]);
    return parity(out.Cp)==parity(out.Ep);
}

Cubie Apply(const Cubie& cube, const Cubie& move)
{
    Cubie result;
    for (int i=0; i<8; ++i)
    {
        result.Cp[i]=cube.Cp[move.Cp[i]];
        result.Co[i]=static_cast<uint8_t>((cube.Co[move.Cp[i]]+move.Co[i])%3);
    }
    for (int i=0; i<12; ++i)
    {
        result.Ep[i]=cube.Ep[move.Ep[i]];
        result.Eo[i]=static_cast<uint8_t>((cube.Eo[move.Ep[i]]+move.Eo[i])%2);
    }
    return result;
}
int RankPermutation(const uint8_t* permutation, int count)
{
    int rank=0;
    for (int i=0; i<count; ++i)
    {
        int smaller=0;
        for (int j=i+1; j<count; ++j) smaller+=permutation[j]<permutation[i];
        rank+=smaller*Factorial[count-1-i];
    }
    return rank;
}
void UnrankPermutation(int rank, int count, uint8_t* permutation)
{
    std::array<uint8_t,8> available{};
    for (int i=0; i<count; ++i) available[i]=static_cast<uint8_t>(i);
    for (int i=0; i<count; ++i)
    {
        const int index=rank/Factorial[count-1-i]; rank%=Factorial[count-1-i];
        permutation[i]=available[index];
        for (int j=index; j<count-1-i; ++j) available[j]=available[j+1];
    }
}
int RankOrientation(const uint8_t* orientations, int count, int base)
{
    int rank=0;
    for (int i=0; i<count-1; ++i) rank=rank*base+orientations[i];
    return rank;
}
void UnrankOrientation(int rank, int count, int base, uint8_t* orientations)
{
    int sum=0;
    for (int i=count-2; i>=0; --i)
    {
        orientations[i]=static_cast<uint8_t>(rank%base); sum+=orientations[i]; rank/=base;
    }
    orientations[count-1]=static_cast<uint8_t>((base-sum%base)%base);
}
bool CentersSolved(const Cube& cube)
{
    for (int face=0; face<6; ++face)
        if (cube.Colors()[face*9+4]!=face) return false;
    return true;
}
bool NormalizeCenters(const Cube& input, Cube& normalized, std::vector<Move>& prefix)
{
    if (input.Order()!=3 || CentersSolved(input)) return true;
    struct Node { Cube State; std::vector<Move> Path; };
    std::queue<Node> queue; queue.push({input,{}});
    std::unordered_set<std::string> seen;
    while (!queue.empty())
    {
        Node node=std::move(queue.front()); queue.pop();
        std::string key;
        for (int face=0; face<6; ++face) key.push_back(static_cast<char>(node.State.Colors()[face*9+4]));
        if (!seen.insert(key).second) continue;
        if (CentersSolved(node.State)) { normalized=std::move(node.State); prefix=std::move(node.Path); return true; }
        for (int axis=0; axis<3; ++axis)
        {
            // The tangent axes are the two axes other than the rotation axis.
            int a=(axis+1)%3, b=(axis+2)%3; if (a>b) std::swap(a,b);
            Node next=node;
            for (int layer=0; layer<3; ++layer)
            {
                Move move{axis,layer,a,b,1}; next.State.Apply(move); next.Path.push_back(move);
            }
            queue.push(std::move(next));
        }
    }
    return false;
}

struct Tables
{
    std::array<Effect,18> Effects{};
    std::vector<std::array<uint16_t,18>> Twist, Flip, Slice;
    std::vector<std::array<uint16_t,10>> CornerPerm, EdgePerm;
    std::array<std::array<uint8_t,10>,24> SlicePerm{};
    std::vector<uint8_t> P1Corner, P1Edge, P2Corner, P2Edge;
    std::array<int16_t,4096> MaskRank{};
    std::array<uint16_t,495> Masks{};
    std::array<int,10> Phase2Moves{};
    int SolvedSlice=0;

    Tables()
    {
        Cube solved(3);
        for (int face=0; face<6; ++face)
        {
            const int axis=FaceAxis(face), layer=FaceSign(face)>0 ? 2 : 0;
            int a=-1,b=-1;
            for (int tangent=0; tangent<3; ++tangent)
                if (tangent!=axis) { if (a<0) a=tangent; else b=tangent; }
            for (int turn=0; turn<3; ++turn)
            {
                const int index=face*3+turn;
                Effects[index].Rotation={axis,layer,a,b,turn==2 ? -1 : turn+1};
                Effects[index].Face=face;
                Cube cube=solved; cube.Apply(Effects[index].Rotation);
                if (!Decode(cube,Effects[index].State)) throw std::logic_error("Cannot decode face turn");
            }
        }
        int p=0;
        for (int face: {U,D}) for (int turn=0; turn<3; ++turn) Phase2Moves[p++]=face*3+turn;
        for (int face: {R,F,L,B}) Phase2Moves[p++]=face*3+1;
        MaskRank.fill(-1);
        int count=0;
        for (int mask=0; mask<4096; ++mask)
        {
            int bits=0;
            for (int value=mask; value; value>>=1) bits+=value&1;
            if (bits==4)
            { MaskRank[mask]=static_cast<int16_t>(count); Masks[count++]=static_cast<uint16_t>(mask); }
        }
        SolvedSlice=MaskRank[0xF00];
        Twist.resize(2187); Flip.resize(2048); Slice.resize(495);
        CornerPerm.resize(40320); EdgePerm.resize(40320);
        BuildMoves();
        BuildPrune(P1Corner,2187,495,18,Twist,Slice,0,SolvedSlice);
        BuildPrune(P1Edge,2048,495,18,Flip,Slice,0,SolvedSlice);
        BuildPrune(P2Corner,40320,24,10,CornerPerm,SlicePerm,0,0);
        BuildPrune(P2Edge,40320,24,10,EdgePerm,SlicePerm,0,0);
    }
    template<class Left, class Right>
    void BuildPrune(std::vector<uint8_t>& prune, int leftSize, int rightSize, int moveCount,
                    const Left& left, const Right& right, int leftGoal, int rightGoal)
    {
        prune.assign(static_cast<size_t>(leftSize)*rightSize,255);
        std::vector<int> frontier; frontier.reserve(prune.size());
        const int goal=leftGoal*rightSize+rightGoal;
        prune[goal]=0; frontier.push_back(goal);
        for (size_t head=0; head<frontier.size(); ++head)
        {
            const int current=frontier[head], a=current/rightSize, b=current%rightSize;
            for (int move=0; move<moveCount; ++move)
            {
                const int next=left[a][move]*rightSize+right[b][move];
                if (prune[next]==255)
                { prune[next]=static_cast<uint8_t>(prune[current]+1); frontier.push_back(next); }
            }
        }
        if (frontier.size()!=prune.size()) throw std::logic_error("Incomplete two-phase pruning table");
    }
    void BuildMoves()
    {
        for (int rank=0; rank<2187; ++rank)
        {
            Cubie cube; UnrankOrientation(rank,8,3,cube.Co.data());
            for (int move=0; move<18; ++move)
                Twist[rank][move]=static_cast<uint16_t>(RankOrientation(Apply(cube,Effects[move].State).Co.data(),8,3));
        }
        for (int rank=0; rank<2048; ++rank)
        {
            Cubie cube; UnrankOrientation(rank,12,2,cube.Eo.data());
            for (int move=0; move<18; ++move)
                Flip[rank][move]=static_cast<uint16_t>(RankOrientation(Apply(cube,Effects[move].State).Eo.data(),12,2));
        }
        for (int rank=0; rank<495; ++rank)
            for (int move=0; move<18; ++move)
            {
                int mask=0;
                for (int position=0; position<12; ++position)
                    if (Masks[rank]&(1<<Effects[move].State.Ep[position])) mask|=1<<position;
                Slice[rank][move]=static_cast<uint16_t>(MaskRank[mask]);
            }
        for (int rank=0; rank<40320; ++rank)
        {
            uint8_t cp[8],ep[8]; UnrankPermutation(rank,8,cp); UnrankPermutation(rank,8,ep);
            for (int step=0; step<10; ++step)
            {
                const auto& effect=Effects[Phase2Moves[step]].State;
                uint8_t nextCp[8],nextEp[8];
                for (int i=0; i<8; ++i)
                { nextCp[i]=cp[effect.Cp[i]]; nextEp[i]=ep[effect.Ep[i]]; }
                CornerPerm[rank][step]=static_cast<uint16_t>(RankPermutation(nextCp,8));
                EdgePerm[rank][step]=static_cast<uint16_t>(RankPermutation(nextEp,8));
            }
        }
        for (int rank=0; rank<24; ++rank)
        {
            uint8_t sp[4]; UnrankPermutation(rank,4,sp);
            for (int step=0; step<10; ++step)
            {
                const auto& effect=Effects[Phase2Moves[step]].State;
                uint8_t next[4];
                for (int i=0; i<4; ++i) next[i]=sp[effect.Ep[8+i]-8];
                SlicePerm[rank][step]=static_cast<uint8_t>(RankPermutation(next,4));
            }
        }
    }
};

struct Search
{
    const Tables& Table;
    const SolveOptions& Options;
    std::chrono::steady_clock::time_point Start;
    size_t Visited=0;
    bool Stopped=false;
    std::vector<int> Path1, Path2;

    bool CheckBudget()
    {
        ++Visited;
        if (Visited>Options.MaxTwoPhaseNodes) { Stopped=true; return false; }
        if ((Visited&1023)!=0) return true;
        Stopped=(Options.Cancel && Options.Cancel->load()) ||
            std::chrono::steady_clock::now()-Start>=Options.TimeLimit;
        return !Stopped;
    }
    bool Phase2(int cp, int ep, int sp, int remaining, int previousFace)
    {
        if (!CheckBudget()) return false;
        if (std::max(Table.P2Corner[cp*24+sp],Table.P2Edge[ep*24+sp])>remaining) return false;
        if (remaining==0) return cp==0 && ep==0 && sp==0;
        for (int step=0; step<10; ++step)
        {
            const int move=Table.Phase2Moves[step], face=Table.Effects[move].Face;
            if (face==previousFace || (previousFace>=0 && face%3==previousFace%3 && face<previousFace)) continue;
            Path2.push_back(move);
            if (Phase2(Table.CornerPerm[cp][step],Table.EdgePerm[ep][step],
                       Table.SlicePerm[sp][step],remaining-1,face)) return true;
            Path2.pop_back();
            if (Stopped) return false;
        }
        return false;
    }
    bool Phase1(int twist, int flip, int slice, int remaining, int previousFace, const Cubie& initial)
    {
        if (!CheckBudget()) return false;
        if (std::max(Table.P1Corner[twist*495+slice],Table.P1Edge[flip*495+slice])>remaining) return false;
        if (remaining==0)
        {
            if (twist!=0 || flip!=0 || slice!=Table.SolvedSlice) return false;
            Cubie cube=initial;
            for (int move:Path1) cube=Apply(cube,Table.Effects[move].State);
            const int cp=RankPermutation(cube.Cp.data(),8);
            const int ep=RankPermutation(cube.Ep.data(),8);
            uint8_t sp[4];
            for (int i=0; i<4; ++i) sp[i]=static_cast<uint8_t>(cube.Ep[8+i]-8);
            const int slicePerm=RankPermutation(sp,4);
            for (int depth=std::max(Table.P2Corner[cp*24+slicePerm],Table.P2Edge[ep*24+slicePerm]); depth<=18; ++depth)
            {
                Path2.clear();
                if (Phase2(cp,ep,slicePerm,depth,previousFace)) return true;
                if (Stopped) return false;
            }
            return false;
        }
        for (int move=0; move<18; ++move)
        {
            const int face=Table.Effects[move].Face;
            if (face==previousFace || (previousFace>=0 && face%3==previousFace%3 && face<previousFace)) continue;
            Path1.push_back(move);
            if (Phase1(Table.Twist[twist][move],Table.Flip[flip][move],
                       Table.Slice[slice][move],remaining-1,face,initial)) return true;
            Path1.pop_back();
            if (Stopped) return false;
        }
        return false;
    }
};
}

SolveResult SolveTwoPhase(const Cube& state, const SolveOptions& options)
{
    using Clock=std::chrono::steady_clock;
    const auto start=Clock::now();
    SolveResult result; result.Method="Kociemba two-phase (state-only)";
    const auto finish=[&](SolveStatus status, const char* message)
    {
        result.Status=status; result.Message=message;
        result.Milliseconds=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
        return result;
    };
    if (options.Cancel && options.Cancel->load()) return finish(SolveStatus::Cancelled,"Cancelled");
    if (state.IsSolved()) return finish(SolveStatus::AlreadySolved,"Already solved");
    if (state.Order()!=2 && state.Order()!=3)
        return finish(SolveStatus::UnsupportedState,"Two-phase solving supports 2x2 and 3x3 cubes");
    Cube normalized=state; std::vector<Move> prefix;
    if (!NormalizeCenters(state,normalized,prefix))
        return finish(SolveStatus::UnsupportedState,"Invalid center orientation");
    Cubie initial;
    if (!Decode(normalized,initial))
        return finish(SolveStatus::UnsupportedState,"Sticker state is not a reachable cube");
    const Tables& table=[]() -> const Tables& { static const Tables value; return value; }();
    Search search{table,options,start};
    int mask=0;
    for (int i=0; i<12; ++i) if (initial.Ep[i]>=8) mask|=1<<i;
    const int twist=RankOrientation(initial.Co.data(),8,3);
    const int flip=RankOrientation(initial.Eo.data(),12,2);
    const int slice=table.MaskRank[mask];
    for (int depth=std::max(table.P1Corner[twist*495+slice],table.P1Edge[flip*495+slice]); depth<=12; ++depth)
    {
        search.Path1.clear();
        if (search.Phase1(twist,flip,slice,depth,-1,initial))
        {
            result.Visited=search.Visited; result.Moves=std::move(prefix);
            for (int move:search.Path1) result.Moves.push_back(table.Effects[move].Rotation);
            for (int move:search.Path2) result.Moves.push_back(table.Effects[move].Rotation);
            if (state.Order()==2)
                for (Move& move:result.Moves) if (move.Layer==2) move.Layer=1;
            result.Moves=Reduce(result.Moves,state.Order());
            Cube verify=state; verify.Apply(result.Moves);
            if (!verify.IsSolved()) throw std::logic_error("Two-phase solution verification failed");
            return finish(SolveStatus::Solved,"Verified two-phase solution");
        }
        if (search.Stopped) break;
    }
    result.Visited=search.Visited;
    if (options.Cancel && options.Cancel->load()) return finish(SolveStatus::Cancelled,"Cancelled");
    if (search.Stopped) return finish(SolveStatus::BudgetExceeded,"Two-phase search budget exhausted");
    return finish(SolveStatus::DepthLimit,"No two-phase solution within search bounds");
}
}
