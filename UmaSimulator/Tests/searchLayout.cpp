//无人岛：不靠手写的建设规则，把所有放得下的本能布局都模拟一遍，找分数最高的目标布局
//
//做法：
//  枚举最终等级（速耐力根智0~5，海之家0~3），按塔克SSR 3破以上（每期4/5/6/6/8格）检查能不能按期建完，
//  只留下"再升任何一级都放不下"的布局；
//  每个布局用同一批随机种子各跑若干局（训练等其他决策仍用手写策略），逐轮淘汰，最后留下的多跑几局。
//  同时给出现在的手写建设规则和攻略模板的分数作对照。
//
//环境变量：
//  UMAAI_LAYOUT_CONFIGS  逗号分隔的配置文件，每套卡组分别搜索，默认 ../ConfigTemplate/testConfig.json
//  UMAAI_LAYOUT_GAMES    第一轮每个布局的局数，默认 1000；之后每轮局数×4，留下的布局÷4
//  UMAAI_LAYOUT_KEEP     最后留下几个布局，默认 8
#include <iostream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <vector>
#include <string>
#include <cmath>
#include <cstdlib>
#include <random>
#include <algorithm>
#include "../Game/Game.h"
#include "../NeuralNet/Evaluator.h"
#include "../GameDatabase/GameDatabase.h"
#include "../GameDatabase/GameConstants.h"
#include "TestConfig.h"
#include "tests.h"
using namespace std;

namespace
{
  int envInt(const char* name, int def)
  {
    const char* v = getenv(name);
    return v != nullptr ? atoi(v) : def;
  }

  struct Layout
  {
    int target[6];
    int priority[6];
    string name;
    vector<double> scores;
    double mean = 0;
  };

  const char* facilityName[6] = { "速","耐","力","根","智","海" };

  string layoutString(const int* t)
  {
    string s;
    for (int f = 0; f < 6; f++)
      s += string(facilityName[f]) + to_string(t[f]) + (f < 5 ? " " : "");
    return s;
  }

  //默认顺序：海之家先建，然后目标等级高的先建
  void defaultPriority(Layout& l)
  {
    vector<int> order = { 0,1,2,3,4 };
    stable_sort(order.begin(), order.end(), [&](int a, int b) {return l.target[a] > l.target[b]; });
    l.priority[MJ_house] = 0;
    for (int i = 0; i < 5; i++)
      l.priority[order[i]] = i + 1;
  }

  //假设每期都建完，按手写策略的目标布局规则模拟5期计划，看能不能达到目标
  bool reachable(const int* target, const int* priority)
  {
    int level[6] = { 0,0,0,0,0,0 };
    for (int phase = 0; phase < 5; phase++)
    {
      int space = GameConstants::MJ_FacilitySpace[2][phase];
      bool picked[6] = { false,false,false,false,false,false };
      while (true)
      {
        int best = -1;
        double bestValue = -1e18;
        for (int f = 0; f < 6; f++)
        {
          if (picked[f] || level[f] >= target[f])continue;
          int cost = MujintoFacility(f, level[f] + 1, false).space();
          if (cost > space)continue;
          bool forced = target[f] - level[f] >= 5 - phase;
          double v = (forced ? 2e5 : 1e5) - 1000.0 * priority[f];
          if (v > bestValue) { bestValue = v; best = f; }
        }
        if (best < 0)break;
        space -= MujintoFacility(best, level[best] + 1, false).space();
        level[best]++;
        picked[best] = true;
      }
    }
    for (int f = 0; f < 6; f++)
      if (level[f] < target[f])return false;
    return true;
  }

  bool reachable(const Layout& l)
  {
    return reachable(l.target, l.priority);
  }

  //用参数P跑n局，第g局的种子是seedBase+g
  vector<double> evaluate(const TestConfig& c, const HandwrittenParams& P, int n, uint64_t seedBase)
  {
    vector<double> scores(n);
    int threadNum = max(1u, thread::hardware_concurrency());
    vector<thread> threads;
    for (int t = 0; t < threadNum; t++)
    {
      threads.push_back(thread([&, t]() {
        for (int i = t; i < n; i += threadNum)
        {
          mt19937_64 rand(seedBase + i);
          Game game;
          game.newGame(rand, GameSettings(), c.umaId, c.umaStars, (int*)&c.cards[0], (int*)&c.zhongmaBlue[0], (int*)&c.zhongmaBonus[0]);
          game.gameSettings.eventStrength = c.eventStrength;
          while (!game.isEnd())
            game.applyAction(rand, Evaluator::handWrittenStrategy(game, P));
          scores[i] = game.finalScore();
        }
        }));
    }
    for (auto& th : threads)th.join();
    return scores;
  }

  HandwrittenParams paramsFor(const Layout& l)
  {
    HandwrittenParams P = handwrittenParams;
    for (int f = 0; f < 6; f++)
    {
      P.mj_target[f] = l.target[f];
      P.mj_priority[f] = l.priority[f];
    }
    return P;
  }

  double mean(const vector<double>& v)
  {
    double s = 0;
    for (double x : v)s += x;
    return s / v.size();
  }

  //配对分差的标准误
  double pairedSE(const vector<double>& a, const vector<double>& b)
  {
    int n = int(a.size());
    double s = 0, s2 = 0;
    for (int i = 0; i < n; i++)
    {
      double d = a[i] - b[i];
      s += d;
      s2 += d * d;
    }
    double m = s / n;
    return sqrt(max(0.0, s2 / n - m * m) / n);
  }

  Layout makeLayout(const string& name, vector<int> t)
  {
    Layout l;
    for (int f = 0; f < 6; f++)l.target[f] = t[f];
    defaultPriority(l);
    l.name = name;
    return l;
  }

  void searchOneDeck(const TestConfig& c, int games1, int keep)
  {
    //枚举所有放得下、而且再升一级就放不下的布局
    vector<Layout> layouts;
    int t[6];
    for (t[0] = 0; t[0] <= 5; t[0]++)
      for (t[1] = 0; t[1] <= 5; t[1]++)
        for (t[2] = 0; t[2] <= 5; t[2]++)
          for (t[3] = 0; t[3] <= 5; t[3]++)
            for (t[4] = 0; t[4] <= 5; t[4]++)
              for (t[5] = 0; t[5] <= 3; t[5]++)
              {
                Layout l;
                for (int f = 0; f < 6; f++)l.target[f] = t[f];
                defaultPriority(l);
                if (!reachable(l))continue;
                bool maximal = true;
                for (int f = 0; f < 6 && maximal; f++)
                {
                  if (t[f] >= (f == MJ_house ? 3 : 5))continue;
                  Layout l2 = l;
                  l2.target[f]++;
                  defaultPriority(l2);
                  if (reachable(l2))maximal = false;
                }
                if (!maximal)continue;
                l.name = layoutString(l.target);
                layouts.push_back(l);
              }
    cout << "放得下的极大布局共 " << layouts.size() << " 个" << endl;

    //逐轮淘汰
    int games = games1;
    uint64_t seedBase = 1234567ull;
    while (true)
    {
      for (auto& l : layouts)
      {
        l.scores = evaluate(c, paramsFor(l), games, seedBase);
        l.mean = mean(l.scores);
      }
      sort(layouts.begin(), layouts.end(), [](const Layout& a, const Layout& b) {return a.mean > b.mean; });
      cout << "每个布局 " << games << " 局，前几名：";
      for (int i = 0; i < min(5, int(layouts.size())); i++)
        cout << " [" << layouts[i].name << "] " << fixed << setprecision(0) << layouts[i].mean;
      cout << endl;
      if (int(layouts.size()) <= keep)break;
      layouts.resize(max(keep, int(layouts.size()) / 4));
      games *= 4;
      seedBase += 1000000007ull;
    }

    //对照：现在的手写建设规则、攻略模板
    vector<Layout> refs;
    {
      Layout cur;
      cur.name = "现在的手写建设规则";
      cur.target[0] = -1;
      refs.push_back(cur);
    }
    refs.push_back(makeLayout("攻略·スピ5编成（速5 耐5 力1 根3 智3 海1）", { 5,5,1,3,3,1 }));
    refs.push_back(makeLayout("攻略·速2耐1力1智1编成（速3 耐5 力5 根1 智3 海1）", { 3,5,5,1,3,1 }));
    refs.push_back(makeLayout("攻略·智多编成（速3 耐3 力5 根1 智5 海1）", { 3,3,5,1,5,1 }));
    for (auto& r : refs)
    {
      if (r.target[0] >= 0 && !reachable(r))
      {
        cout << r.name << "：按期放不下" << endl;
        r.mean = -1;
        continue;
      }
      HandwrittenParams P = handwrittenParams;
      if (r.target[0] >= 0)P = paramsFor(r);
      r.scores = evaluate(c, P, games, seedBase);
      r.mean = mean(r.scores);
    }

    const Layout& best = layouts[0];
    cout << endl << "最终（每个 " << games << " 局，同一批种子，括号里是和第一名的分差及其误差）：" << endl;
    for (auto& l : layouts)
      cout << "  " << l.name << "  " << fixed << setprecision(0) << l.mean << "（" << l.mean - best.mean << " ±" << pairedSE(l.scores, best.scores) << "）" << endl;
    cout << "对照：" << endl;
    for (auto& r : refs)
      if (r.mean >= 0)
        cout << "  " << r.name << "  " << r.mean << "（" << r.mean - best.mean << " ±" << pairedSE(r.scores, best.scores) << "）" << endl;
    HandwrittenParams P = paramsFor(best);
    P.saveJson("handwrittenParams_layout.json");
    cout << "第一名的布局已写入 handwrittenParams_layout.json" << endl;
  }
}

void main_searchLayout()
{
  GameDatabase::loadTranslation("../db/text_data.json");
  GameDatabase::loadUmas("../db/umaDB.json");
  GameDatabase::loadDBCards("../db/cardDB.json");

  const char* configEnv = getenv("UMAAI_LAYOUT_CONFIGS");
  string configList = configEnv != nullptr ? configEnv : "../ConfigTemplate/testConfig.json";
  int games1 = envInt("UMAAI_LAYOUT_GAMES", 1000);
  int keep = envInt("UMAAI_LAYOUT_KEEP", 8);
  stringstream ss(configList);
  string path;
  while (getline(ss, path, ','))
  {
    if (path.empty())continue;
    cout << "==== 卡组 " << path << endl;
    searchOneDeck(TestConfig::loadFile(path), games1, keep);
  }
}
