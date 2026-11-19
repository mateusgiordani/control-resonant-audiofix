# Control Resonant AudioFix

Correção gratuita e open-source para o **áudio engasgando / dessincronizado / música cortando** no
*Control Resonant* no PC. Sem PrimoCache, sem driver, sem programa rodando em segundo plano: só uma
`version.dll` na pasta do jogo.

🇺🇸 [English](README.md)

## Resumo

1. Baixe `ControlResonant-AudioFix.zip` em [Releases](../../releases).
2. Extraia e rode `install.ps1` (botão direito → *Executar com PowerShell*), **ou** copie
   `version.dll` e `audiofix.ini` para a pasta do jogo (junto do `CONTROLResonant.exe`).
3. Jogue. Para remover: `uninstall.ps1` ou apague a `version.dll` da pasta do jogo.

## Causa

O jogo lê seus dados de 63 pacotes `.rmdblob` (~106 GB) e os abre com `FILE_FLAG_NO_BUFFERING`, que
**desliga o cache de arquivos do Windows em RAM**. Só que o jogo relê os mesmos dados o tempo todo:

| Medido em um PC (32 GB RAM, NVMe) | |
|---|---|
| Dados pedidos pelo jogo nos primeiros **11 minutos** | **160 GB** |
| Taxa enquanto anda pelo mapa | **165–310 MB/s** |
| Tamanho dos pacotes no disco | 106 GB |

Sem cache, tudo vai ao SSD. Depois de uns 20 minutos a fila do disco satura, o streaming de áudio
perde a corrida e aparecem o stutter, o desync e a música cortando. O mesmo padrão aconteceu no
*Alan Wake 2* (mesma engine Northlight). É por isso que o PrimoCache funciona: ele põe um cache em
RAM *abaixo* dessa flag. Esta correção remove a flag e deixa o cache do próprio Windows trabalhar.

## Como funciona

O executável importa `VERSION.dll`, que não é *KnownDLL*, então a cópia na pasta do jogo tem
prioridade. A proxy:

1. **repassa** as 17 funções para `C:\Windows\System32\version.dll`;
2. **intercepta** `CreateFileW`/`CreateFileA` na tabela de importação (IAT) do executável, sem
   alterar código nem arquivos do jogo;
3. só em aberturas **somente-leitura** de `.rmdblob`, remove `FILE_FLAG_NO_BUFFERING` e adiciona
   `FILE_FLAG_RANDOM_ACCESS`;
4. grava `audiofix.log` com as aberturas e estatísticas a cada 60 s.

No PC de teste: o jogo seguia pedindo 150–300 MB/s, mas a leitura real no disco caiu para
**0–10 MB/s** (disco 0–9% ocupado), e o áudio ficou sincronizado.

## Efeitos colaterais

- **A primeira visita a uma área ainda lê do disco**, então pode ter um soluço breve quando uma zona nova carrega.
- **Um pouco mais de CPU**: a leitura via cache faz uma cópia de memória a mais. É pouco, mas não é zero.
- **O Windows usa a RAM livre como cache.** O Gerenciador de Tarefas mostra mais "em cache" e menos
  "livre". É normal: essa memória é devolvida na hora se algum programa precisar. Não existe
  reserva fixa como no PrimoCache. Com 16 GB de RAM o ganho pode ser menor.
- **Leituras assíncronas podem terminar na hora**: comportamento válido e documentado. O jogo lidou
  bem nos testes, mas o timing muda.
- **Conflito com outros mods que usam `version.dll`**: o instalador se recusa a sobrescrever.
- **Atualizações do jogo** não removem a DLL. Se a Remedy corrigir o bug, é só apagar.
- **Sem risco de perda de dados**: nada é escrito via cache (diferente do *Defer-Write* do PrimoCache).
- O jogo não tem anti-cheat. É uma proxy DLL comum, como ReShade/SpecialK. Use por sua conta e risco, como qualquer mod.

## É seguro? / Verificando o download

Desconfiar de uma DLL da internet é o instinto certo. Dá para conferir esta:

- **É pequena e legível:** toda a lógica está em [`src/audiofix.c`](src/audiofix.c), cerca de 300
  linhas de C, sem código de rede. Ela só escreve o `audiofix.log` e só muda as flags de abertura dos
  `.rmdblob` somente-leitura.
- **As releases são compiladas pelo GitHub Actions**, não no PC de alguém, com o log do build público.
- **Cada release tem um atestado de build assinado**, que prova que a DLL saiu deste código:
  ```
  gh attestation verify version.dll --repo mateusgiordani/control-resonant-audiofix
  ```
  Um arquivo alterado falha nessa verificação. Os hashes SHA-256 estão no `SHA256SUMS.txt` de cada release.
- **VirusTotal:** Procure o SHA-256 do `SHA256SUMS.txt` no [virustotal.com](https://www.virustotal.com/) ou envie a DLL você mesmo.
- **Não confia em binário nenhum?** Compile você mesmo (abaixo).

## Compilar

Visual Studio 2022 Build Tools: `build.bat` (gera `build\version.dll`) e `build.bat test`.

## Reporte para a Remedy

Isto é um contorno, não a correção real: <https://remedy.helpshift.com/hc/en/5-control-resonant/>

Licença MIT. Sem afiliação com a Remedy Entertainment.
