# 无人岛剧本适配（进行中）

本分支基于 Legend 分支，正在适配「無人島へようこそ」剧本，剧本机制参考 [umasim](https://github.com/mee1080/umasim)。

## Linux 编译与运行

```bash
sudo apt install build-essential cmake zlib1g-dev libasio-dev   # libasio-dev 可选，没有时不支持 websocket 模式
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

- `build/UmaAiTestScore`：用手写逻辑自对局测分，在 `build/` 目录下运行（读取 `../db` 和 `../ConfigTemplate/testConfig.json`）。
- `build/UmaAi`：主程序，在放有 `db/`、`aiConfig_cpu.json`（可从 `ConfigTemplate` 复制）的目录下运行，默认读取同目录的 `thisTurn.json`。

## 支援卡数据

新卡可以用 `Scripts/import_umasim_card/import_cards.py` 从 umasim 的 `data/support_card.txt` 导入（只导入各突破的数值，固有效果需另行处理）。

---

# 2024.6.26 New scenario started. UmaAi needs to be updated
### What is needed?
#### 1.Analyzing scenario mechanics
1.1 Formula of status/vegetable/... gain. Probability of the buffs of big success cooking. ......   
1.2 Events at a fixed turn of scenario    
1.3 List of parameters    
#### 2.Scenario Simulator
Mainly Game.h/cpp
#### 3.Search,Handwritten-logic,....
#### 4.OCR: Connecting the game and UmaAI
I will be very glad if someone can help me do this. I have no experience with OCR.





## Manual-input version updated on 2024.6.3. See the release page.
introduction will be written later. 

This tool is just for **algorithm learning**. Don't use it for illegal purpose.   
You can compile this tool by yourself. Change the settings in config.h, then you can evaluate the cardset strength or have a simulation game with this program.    
You can also learn the game mechanisms(such as the formula of training status gain) from the source code (mainly Game.h/cpp)

Although it's just a "very advanced calculator" which can **never change** the status of the game. Many people think this is an **"illegal tool"(不正ツール)**.    
So I would **NOT** provide more information for the installation.    
Later I will publish a **"absolutely legal"** version, whose game status is inputed **manually** so it will need no modification to the game program itself.   
