# DirectX 12 PBF Fluid Simulation

DirectX 12 Compute Shader를 기반으로 구현한 **GPU Position Based Fluids(PBF) 유체 시뮬레이션 프로젝트**입니다.

PBF 기반 유체 물리 연산뿐만 아니라 GPU Neighbor Search, Particle Sorting, Screen Space Fluid Rendering(SSFR), GPU-Driven Diffuse Particle Pipeline까지 하나의 실시간 파이프라인으로 구성했습니다.

* 개발 기간: **2026.02.01 ~ 2026.02.21**
* Language: **C++**
* Graphics API: **DirectX 12**
* Shader: **HLSL / Compute Shader**
* Profiling: **PIX**

---

## Demo Video

[![PBF Fluid Simulation Demo](https://img.youtube.com/vi/VIDEO_ID/maxresdefault.jpg)](https://www.youtube.com/watch?v=kDXEbfrF-uI)

---

## 주요 구현 내용

* DirectX 12 Compute Shader 기반 **PBF Fluid Solver**
* Uniform Grid / Spatial Hash 기반 **GPU Neighbor Search**
* GPU Sorting 기반 Particle 재배열 및 Cell Range 구성
* Density Constraint / Lambda / Position Correction 병렬 처리
* Vorticity / XSPH Viscosity 적용
* **Screen Space Fluid Rendering(SSFR)**
* Spray / Foam / Bubble을 처리하는 **Diffuse Particle Simulation**
* Compaction 및 `ExecuteIndirect` 기반 **GPU-Driven Rendering**
* PIX 기반 GPU Profiling 및 병목 분석
* 최대 **100만 Fluid Particle** 규모 실시간 시뮬레이션 테스트

---

## PBF Simulation Pipeline

한 프레임의 주요 Simulation Pipeline은 다음과 같이 구성했습니다.

```text
Integration
    ↓
Spatial Hashing
    ↓
GPU Sorting / Particle Permutation
    ↓
Grid Construction
    ↓
Density & Lambda
    ↓
Delta Position
    ↓
Constraint Apply
    ↓
Vorticity
    ↓
Velocity Update
```

### 1. Integration

중력 등의 외력을 적용하여 각 Particle의 임시 위치인 `Predicted Position`을 계산합니다.

가변적인 Frame Time에서도 Simulation 안정성을 유지하기 위해 Fixed Timestep 기반 Sub-stepping 구조를 사용했습니다.

### 2. Spatial Hashing & GPU Sorting

각 Particle의 위치를 Uniform Grid Cell에 매핑하고 Spatial Hash 값을 계산합니다.

이후 Particle을 Cell 단위로 정렬하여 동일하거나 인접한 Cell의 Particle이 메모리상에서 가깝게 배치되도록 구성했습니다.

이를 통해 PBF Solver에서 전체 Particle을 탐색하지 않고 주변 Grid Cell만 탐색하도록 Neighbor Search 범위를 제한했습니다.

### 3. Density Constraint Solver

각 Particle에 대해 주변 Particle을 탐색하여 Density를 계산하고 PBF의 Density Constraint를 해결합니다.

Solver Iteration에서는 다음 과정을 반복합니다.

```text
Density 계산
    ↓
Lambda 계산
    ↓
Delta Position 계산
    ↓
Constraint 적용
```

Kernel 함수는 Poly6 / Spiky 계열을 사용했으며, Position Correction 과정에서 Tensile Instability 보정을 함께 적용했습니다.

### 4. Vorticity & Velocity Update

Position Constraint 해결 이후 보정된 위치를 기반으로 Velocity를 갱신합니다.

추가적으로 다음 효과를 적용했습니다.

* Vorticity
* XSPH Viscosity
* 외력 및 Collision 결과 반영

---

## GPU Neighbor Search

PBF에서는 Particle 하나가 주변 Particle과 반복적으로 상호작용하기 때문에 Neighbor Search의 비용이 전체 Solver 성능에 큰 영향을 줍니다.

전체 Particle을 탐색하는 대신 다음 구조를 사용했습니다.

```text
Particle Position
    ↓
Grid Cell 계산
    ↓
Spatial Hash 생성
    ↓
GPU Sorting
    ↓
Cell Range 구성
    ↓
Neighbor Cell 탐색
```

Particle을 Grid 기준으로 재배열한 뒤 각 Cell의 범위를 구성하여, Solver에서는 현재 Particle 주변의 Grid Cell만 탐색하도록 구현했습니다.

---

## GPU Sorting Optimization

초기 구현에서는 GPU 병렬 정렬을 위해 Bitonic Sort를 사용했습니다.

하지만 Particle 수가 증가하면서 매 Frame 수행되는 Sorting이 주요 병목으로 나타났습니다.

### Shared Memory 기반 Bitonic Sort 개선

초기 Bitonic Sort는 작은 Stride 단계에서도 반복적으로 Global Memory에 접근하고 여러 번 Dispatch해야 하는 문제가 있었습니다.

초기 정렬 단계를 Group Shared Memory에서 처리하도록 변경하여 Global Memory 접근과 Dispatch 횟수를 줄였습니다.

10만 Particle 기준 PIX 측정 결과:

| 방식                 | Sorting Time |
| ------------------ | -----------: |
| Naive Bitonic Sort |      1.52 ms |
| Shared Memory 적용   |      1.01 ms |

약 **33.6%**의 Sorting Time 감소를 확인했습니다.

### Counting Sort 기반 구조로 변경

100만 Particle 규모에서는 Bitonic Sort의 비교 연산량 자체가 병목이 되었습니다.

Spatial Hash 값의 특성을 이용하여 Particle의 목적지 Index를 계산하고 재배열하는 **Counting Sort 기반 Pipeline**으로 변경했습니다.

100만 Particle (`2^20 = 1,048,576`) 기준:

| 방식            | Sorting Time |
| ------------- | -----------: |
| Bitonic Sort  |      5.19 ms |
| Counting Sort |      0.68 ms |

Sorting Time을 약 **86.9% 감소**시켰으며 약 **7.6배**의 성능 차이를 확인했습니다.

---

## Screen Space Fluid Rendering

Particle을 Marching Cubes와 같은 Polygon Mesh로 변환하지 않고, Screen Space에서 Fluid Surface를 재구성했습니다.

Rendering Pipeline은 다음과 같습니다.

```text
Fluid Depth
    ↓
Depth Smoothing
    ↓
Fluid Thickness
    ↓
Normal Reconstruction
    ↓
Reflection / Refraction
    ↓
Composite
    ↓
Tone Mapping
```

### Fluid Depth

각 Fluid Particle을 Camera-facing Quad 형태로 Rendering하여 Linear Depth Map을 생성합니다.

### Depth Smoothing

Particle 단위로 생성된 거친 Depth Surface를 부드럽게 만들기 위해 Separable Bilateral Blur를 적용했습니다.

```text
Horizontal Blur
    ↓
Vertical Blur
```

Depth Edge를 가능한 유지하면서 Fluid Surface를 부드럽게 재구성합니다.

### Thickness

Particle의 Thickness를 Additive Blending으로 누적하여 Fluid의 광학 처리에 사용할 Thickness Map을 생성합니다.

### Composite

Smoothing된 Depth의 화면 공간 미분을 이용하여 Normal을 복원하고 다음 효과를 합성합니다.

* Fresnel Reflection
* Refraction
* Environment Reflection
* Fluid Transparency
* HDR Tone Mapping

---

## GPU-Driven Diffuse Particle Pipeline

Fluid Simulation의 시각적 표현을 위해 난류에 의해 생성되는 Diffuse Particle을 추가했습니다.

Diffuse Particle은 주변 Fluid Particle의 상태에 따라 다음 세 종류로 분류됩니다.

### Spray

공기 중으로 튀어나온 Particle입니다.

* Gravity
* Air Drag

를 적용하여 탄도 운동하도록 구성했습니다.

### Bubble

Fluid 내부에 위치한 Particle입니다.

주변 Fluid Velocity Field를 따라 이동하며 Buoyancy를 적용합니다.

### Foam

Fluid Surface 부근에 존재하는 Particle입니다.

Fluid Surface를 따라 이동하도록 구성했습니다.

---

## Diffuse Particle Pipeline

```text
Diffuse Generation
    ↓
Update
    ↓
Spray / Bubble / Foam Classification
    ↓
Compaction
    ↓
Build Indirect Arguments
    ↓
ExecuteIndirect
```

Diffuse Particle은 매 Frame 생성 및 소멸되어 활성 Particle 수가 계속 변합니다.

Particle Count를 CPU로 Readback한 뒤 다시 Draw Call을 구성할 경우 CPU-GPU Synchronization 비용이 발생할 수 있기 때문에, GPU에서 유효 Particle 수와 Indirect Argument를 생성하도록 구성했습니다.

`ExecuteIndirect`를 이용하여 CPU Particle Count Readback 없이 Compute / Rendering 작업을 수행하도록 구현했습니다.

---

## Shader ALU Optimization

PBF Solver의 Neighbor Loop는 Particle 수가 증가할수록 매우 많은 횟수로 실행됩니다.

따라서 반복문 내부의 작은 연산 비용도 전체 GPU Frame Time에 영향을 줄 수 있습니다.

### `pow()` 제거

고정된 지수 연산에 `pow()`를 사용하는 대신 직접 곱셈으로 전개했습니다.

```hlsl
// Before
float sCorr = -k * pow(ratio, 4.0);

// After
float ratio2 = ratio * ratio;
float ratio4 = ratio2 * ratio2;
float sCorr = -k * ratio4;
```

### 중복 Normalize 연산 제거

Neighbor Distance를 이미 계산한 경우 해당 값을 재사용하여 불필요한 연산을 줄였습니다.

```hlsl
// Before
float3 dir = normalize(rVec);

// After
float3 dir = rVec / r;
```

---

## Performance

### Test Environment

|     |                                        |
| --- | -------------------------------------- |
| OS  | Windows 11 64-bit                      |
| CPU | Intel Core i9-13900HX                  |
| GPU | NVIDIA GeForce RTX 4060 Laptop GPU 8GB |

### 1M Fluid Particle

Fluid Particle 약 **100만 개** 기준:

| Pass          |     GPU Time |
| ------------- | -----------: |
| PBF Compute   |     ~25.2 ms |
| SSFR Graphics |      ~3.2 ms |
| **Total**     | **~28.4 ms** |

약 33.3ms의 30 FPS Frame Budget 이내에서 Simulation과 Rendering을 처리했습니다.

### PBF Compute Breakdown

| Pass                               | GPU Time |
| ---------------------------------- | -------: |
| Integration                        |  ~0.2 ms |
| Counting Sort                      |  ~0.7 ms |
| Permute & CopyBack                 |  ~0.4 ms |
| Build Grid                         |  ~0.4 ms |
| Neighbor Search / Solver Iteration | ~14.0 ms |
| Vorticity                          |  ~3.4 ms |
| Velocity / Position Update         |  ~6.1 ms |

가장 큰 비용은 Neighbor Search 및 PBF Solver Iteration에서 발생했습니다.

### SSFR Breakdown

| Pass           |  GPU Time |
| -------------- | --------: |
| Shadow Map     |   ~1.0 ms |
| Background     |  ~0.08 ms |
| Fluid Depth    |   ~1.0 ms |
| Bilateral Blur |   ~0.1 ms |
| Thickness      |   ~0.9 ms |
| Composite      | ~0.072 ms |
| Tone Mapping   |  ~0.05 ms |

---

## Performance Trade-off

Diffuse Particle은 난류 상황에 따라 활성 Particle 수가 크게 변합니다.

Stress Test에서 약 180만 개의 활성 Diffuse Particle이 존재하는 경우 약 **12.2 ms**의 추가 GPU 비용이 발생했습니다.

```text
1M Fluid only
≈ 28.4 ms

1M Fluid + 1.8M Diffuse
≈ 40.6 ms
```

따라서 1M Fluid 환경에서 30 FPS Frame Budget을 유지하려면 Diffuse Particle의 최대 활성 개수를 제한하는 방식의 Trade-off가 필요하다는 점을 확인했습니다.

---

## 프로젝트에서 다룬 주요 문제

이 프로젝트에서는 단순히 PBF Algorithm을 구현하는 것뿐 아니라 Particle 수를 증가시키면서 발생하는 GPU Pipeline 병목을 분석하고 개선하는 데 중점을 두었습니다.

특히 다음 문제를 다뤘습니다.

* 대규모 Particle 환경에서 GPU Sorting 병목
* Neighbor Search의 메모리 접근 비용
* Compute Shader 내부 반복 연산 비용
* 매 Frame 변하는 Diffuse Particle Count 처리
* CPU-GPU Readback에 따른 Synchronization 문제
* 대규모 Particle Fluid Rendering 비용
* Simulation과 Rendering의 Frame Budget 분배

---

## References

본 프로젝트의 Simulation 및 Rendering 구현에는 다음 자료를 참고했습니다.

* **Position Based Fluids**
* **Unified Spray, Foam and Bubbles for Particle-Based Fluids**
* **Screen Space Fluid Rendering for Games**

---

## Assets & Credits

- HDRI: [Day Sky HDRI 027 A](https://ambientcg.com/view?id=DaySkyHDRI027A) — ambientCG, CC0