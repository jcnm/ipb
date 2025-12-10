# Proposition : MQTT v5 Native pour IPB

## Contexte

IPB cible l'industrie bas-niveau où la pression mémoire et temporelle est critique.
L'utilisation de Paho MQTT (bibliothèque générique) introduit des compromis :
- Allocations dynamiques fréquentes
- Abstractions coûteuses en latence
- Empreinte mémoire non optimisée
- Pas de contrôle fin sur le threading model

## Option 1 : Implémentation Native Complète (21-29 semaines)

### Architecture Proposée

```
┌────────────────────────────────────────────────────────────────────────────┐
│                        libipb-mqtt-core (Native)                           │
├────────────────────────────────────────────────────────────────────────────┤
│                                                                            │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                     Zero-Copy Packet Layer                          │   │
│  │  ┌───────────────┐  ┌───────────────┐  ┌───────────────┐           │   │
│  │  │ PacketView    │  │ PacketBuilder │  │ VarIntCodec   │           │   │
│  │  │ (no alloc)    │  │ (static buf)  │  │ (inline)      │           │   │
│  │  └───────────────┘  └───────────────┘  └───────────────┘           │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                            │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                      Memory Management                              │   │
│  │  ┌───────────────┐  ┌───────────────┐  ┌───────────────┐           │   │
│  │  │ FixedPool<T>  │  │ RingBuffer    │  │ StaticString  │           │   │
│  │  │ (compile-time)│  │ (lock-free)   │  │ (no heap)     │           │   │
│  │  └───────────────┘  └───────────────┘  └───────────────┘           │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                            │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                    Protocol State Machines                          │   │
│  │  ┌───────────────┐  ┌───────────────┐  ┌───────────────┐           │   │
│  │  │ QoS0Handler   │  │ QoS1Handler   │  │ QoS2Handler   │           │   │
│  │  │ (stateless)   │  │ (ID tracking) │  │ (4-way FSM)   │           │   │
│  │  └───────────────┘  └───────────────┘  └───────────────┘           │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                            │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                      I/O Abstraction                                │   │
│  │  ┌───────────────┐  ┌───────────────┐  ┌───────────────┐           │   │
│  │  │ SocketIO      │  │ TLSWrapper    │  │ EventLoop     │           │   │
│  │  │ (epoll/kqueue)│  │ (mbedTLS)     │  │ (single-thrd) │           │   │
│  │  └───────────────┘  └───────────────┘  └───────────────┘           │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                            │
└────────────────────────────────────────────────────────────────────────────┘
```

### Caractéristiques Clés

1. **Zero-Allocation Hot Path**
   ```cpp
   // Parsing sans allocation - vue sur buffer existant
   class PacketView {
       std::span<const uint8_t> data_;
   public:
       constexpr PacketType type() const noexcept;
       constexpr std::span<const uint8_t> payload() const noexcept;
       constexpr PropertyIterator properties() const noexcept;
   };
   ```

2. **Memory Pools Compile-Time**
   ```cpp
   // Pool de messages pré-alloué
   template<size_t MaxMessages, size_t MaxPayloadSize>
   class MessagePool {
       std::array<Message, MaxMessages> messages_;
       std::bitset<MaxMessages> allocated_;
       // Lock-free allocation avec atomics
   };
   ```

3. **Lock-Free Message Passing**
   ```cpp
   // SPSC ring buffer pour publish sans lock
   template<typename T, size_t Capacity>
   class SPSCQueue {
       alignas(64) std::atomic<size_t> head_;
       alignas(64) std::atomic<size_t> tail_;
       std::array<T, Capacity> buffer_;
   };
   ```

4. **Latence Prédictible**
   ```cpp
   // Deadline-aware publish
   struct PublishOptions {
       std::chrono::microseconds max_latency{100};
       bool drop_if_exceeded = false;
       QoS fallback_qos = QoS::AT_MOST_ONCE;  // Dégrade si timeout
   };
   ```

### Empreinte Mémoire Estimée

| Configuration | RAM Statique | RAM Dynamique | Total |
|--------------|--------------|---------------|-------|
| **Minimal** (10 subs, 100 msg inflight) | 32 KB | 0 | 32 KB |
| **Standard** (100 subs, 1000 msg inflight) | 256 KB | 0 | 256 KB |
| **Full** (1000 subs, 10000 msg inflight) | 2 MB | 0 | 2 MB |

### Latence Estimée (99th percentile)

| Opération | Paho MQTT | IPB Native | Amélioration |
|-----------|-----------|------------|--------------|
| Publish QoS 0 | 50-200 µs | 5-15 µs | 10-15x |
| Publish QoS 1 | 100-500 µs | 20-50 µs | 5-10x |
| Receive + Parse | 30-100 µs | 3-10 µs | 10x |
| Topic Match | 10-50 µs | 1-5 µs | 10x |

## Option 2 : Wrapper Optimisé sur Bibliothèque Existante (6-8 semaines)

Plutôt qu'une implémentation from-scratch, wrapper une bibliothèque C légère.

### Candidats

| Bibliothèque | Licence | MQTT Version | Empreinte | Qualité |
|--------------|---------|--------------|-----------|---------|
| **mqtt-c** | MIT | 3.1.1 + partial v5 | ~15 KB | ★★★☆☆ |
| **NanoMQ (nng)** | MIT | v5 complet | ~50 KB | ★★★★☆ |
| **wolfMQTT** | GPLv2/Commercial | v5 complet | ~20 KB | ★★★★★ |
| **coreMQTT (AWS)** | MIT | v5 complet | ~25 KB | ★★★★★ |

### Recommandation : coreMQTT

```cpp
// Wrapper IPB autour de coreMQTT
namespace ipb::transport::mqtt::native {

class CoreMQTTWrapper {
    MQTTContext_t context_;

    // Buffers statiques
    std::array<uint8_t, 4096> network_buffer_;
    std::array<MQTTSubscribeInfo_t, 64> subscriptions_;

public:
    // API IPB
    Result<> connect(const ConnectionConfig& config);
    Result<> publish(std::span<const uint8_t> payload, QoS qos);
    Result<> subscribe(std::string_view topic, MessageCallback cb);

    // Zero-copy receive
    void process_incoming(std::span<const uint8_t> data);
};

}
```

### Avantages coreMQTT

1. **Conçu pour embedded** - Pas de malloc dans le code library
2. **MQTT v5 complet** - Toutes les features
3. **Transport agnostic** - On fournit notre I/O
4. **MIT License** - Compatible commercial
5. **Maintenu par AWS** - Qualité industrielle, tests extensifs
6. **FreeRTOS compatible** - Prouve l'orientation embedded

## Option 3 : Hybride (Recommandée) - 8-12 semaines

Combiner le meilleur des deux mondes :

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    libipb-transport-mqtt (Refactored)                   │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │                     Backend Abstraction                         │   │
│  │                                                                 │   │
│  │   ┌─────────────┐    ┌─────────────┐    ┌─────────────┐        │   │
│  │   │ PahoBackend │    │ CoreMQTT    │    │ NativeIPB   │        │   │
│  │   │ (existing)  │    │ Backend     │    │ Backend     │        │   │
│  │   │             │    │ (new)       │    │ (future)    │        │   │
│  │   └──────┬──────┘    └──────┬──────┘    └──────┬──────┘        │   │
│  │          │                  │                  │               │   │
│  │          └──────────────────┼──────────────────┘               │   │
│  │                             ▼                                  │   │
│  │                    ┌─────────────────┐                         │   │
│  │                    │ IMQTTBackend    │                         │   │
│  │                    │ interface       │                         │   │
│  │                    └────────┬────────┘                         │   │
│  │                             │                                  │   │
│  └─────────────────────────────┼───────────────────────────────────┘   │
│                                │                                       │
│  ┌─────────────────────────────┼───────────────────────────────────┐   │
│  │                             ▼                                   │   │
│  │                    MQTTConnectionManager                        │   │
│  │          (API existante - aucun changement pour utilisateurs)   │   │
│  │                                                                 │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘

Configuration CMake:
  -DIPB_MQTT_BACKEND=paho     # Défaut actuel
  -DIPB_MQTT_BACKEND=coremqtt # Nouveau backend embedded
  -DIPB_MQTT_BACKEND=native   # Future implémentation maison
```

## Métriques de Décision

### Critères pour IPB Industriel

| Critère | Poids | Paho | coreMQTT | Native IPB |
|---------|-------|------|----------|------------|
| Latence p99 | 30% | ★★☆ | ★★★★ | ★★★★★ |
| Empreinte mémoire | 25% | ★★☆ | ★★★★ | ★★★★★ |
| Temps de développement | 20% | ★★★★★ | ★★★★ | ★☆☆ |
| Maintenabilité | 15% | ★★★★ | ★★★★ | ★★★ |
| MQTT v5 complet | 10% | ★★★★ | ★★★★★ | ★★★★★ |
| **Score pondéré** | 100% | **2.95** | **4.05** | **3.70** |

### Recommandation Finale

**Court terme (2-3 mois)** : Option 3 Hybride avec coreMQTT
- Ajouter backend coreMQTT à libipb-transport-mqtt
- Conserver Paho comme fallback
- Sélection à la compilation

**Moyen terme (6-12 mois)** : Si les benchmarks montrent des gains significatifs
- Développer le backend natif IPB progressivement
- Module par module (parser, QoS, etc.)

**Critères de succès pour passer au natif** :
- Latence p99 publish < 10 µs
- Empreinte mémoire < 64 KB pour 100 subscriptions
- Conformité MQTT v5 > 95% des tests Eclipse Paho

## Prototype : Header Only Zero-Alloc Parser

```cpp
// ipb/mqtt/native/packet_view.hpp
#pragma once

#include <cstdint>
#include <span>
#include <optional>

namespace ipb::mqtt::native {

enum class PacketType : uint8_t {
    CONNECT     = 1,
    CONNACK     = 2,
    PUBLISH     = 3,
    PUBACK      = 4,
    PUBREC      = 5,
    PUBREL      = 6,
    PUBCOMP     = 7,
    SUBSCRIBE   = 8,
    SUBACK      = 9,
    UNSUBSCRIBE = 10,
    UNSUBACK    = 11,
    PINGREQ     = 12,
    PINGRESP    = 13,
    DISCONNECT  = 14,
    AUTH        = 15
};

// Zero-allocation variable integer decoder
constexpr std::pair<uint32_t, size_t> decode_varint(std::span<const uint8_t> data) noexcept {
    uint32_t value = 0;
    size_t bytes = 0;
    uint32_t multiplier = 1;

    for (size_t i = 0; i < std::min(data.size(), size_t{4}); ++i) {
        value += (data[i] & 0x7F) * multiplier;
        multiplier *= 128;
        ++bytes;
        if ((data[i] & 0x80) == 0) break;
    }

    return {value, bytes};
}

// Zero-allocation packet view
class PacketView {
    std::span<const uint8_t> data_;

public:
    constexpr explicit PacketView(std::span<const uint8_t> data) noexcept
        : data_(data) {}

    constexpr bool is_valid() const noexcept {
        return data_.size() >= 2;
    }

    constexpr PacketType type() const noexcept {
        return static_cast<PacketType>((data_[0] >> 4) & 0x0F);
    }

    constexpr uint8_t flags() const noexcept {
        return data_[0] & 0x0F;
    }

    constexpr std::span<const uint8_t> remaining() const noexcept {
        auto [len, bytes] = decode_varint(data_.subspan(1));
        return data_.subspan(1 + bytes, len);
    }

    // PUBLISH specific (QoS from flags)
    constexpr uint8_t qos() const noexcept {
        return (flags() >> 1) & 0x03;
    }

    constexpr bool retain() const noexcept {
        return flags() & 0x01;
    }

    constexpr bool dup() const noexcept {
        return flags() & 0x08;
    }
};

// Topic view (zero-copy string)
class TopicView {
    std::span<const uint8_t> data_;

public:
    constexpr explicit TopicView(std::span<const uint8_t> data) noexcept
        : data_(data) {}

    constexpr size_t length() const noexcept {
        return (data_[0] << 8) | data_[1];
    }

    constexpr std::string_view str() const noexcept {
        return {reinterpret_cast<const char*>(data_.data() + 2), length()};
    }

    // Wildcard matching (compile-time friendly)
    constexpr bool matches(std::string_view pattern) const noexcept;
};

} // namespace ipb::mqtt::native
```

## Conclusion

Pour IPB industriel avec contraintes temps-réel :

1. **Ne pas réinventer la roue** - coreMQTT est excellent
2. **Abstraire le backend** - Flexibilité future
3. **Mesurer avant d'optimiser** - Benchmarks sur cas d'usage réels
4. **Natif si justifié** - Seulement si coreMQTT insuffisant

Le surcoût de développement d'une implémentation 100% native (21-29 semaines)
n'est justifiable que si :
- Latence < 5 µs requise
- Mémoire < 32 KB imposée
- Certification sécurité spécifique (DO-178C, IEC 62443)
