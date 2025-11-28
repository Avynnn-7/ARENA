export function ArenaMark({ size = 28, className = '' }: { size?: number; className?: string }) {
  return (
    <img src="/arena_logo.png" width={size} height={size} className={className} alt="Arena Logo" aria-hidden="true" />
  );
}

export function ArenaLogo({ size = 28, showWord = true, className = '' }: { size?: number; showWord?: boolean; className?: string }) {
  return (
    <span className={`inline-flex items-center gap-2.5 ${className}`}>
      <ArenaMark size={size} />
      {showWord && (
        <span className="font-mono font-bold tracking-[0.18em]" style={{ fontSize: size * 0.6 }}>
          ARENA
        </span>
      )}
    </span>
  );
}
